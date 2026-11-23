#!/usr/bin/env python3
"""BS4 air and water panels (packet P6: src/engine/bs4panel_air.cpp, bs4panel_water.cpp) with the real game logic (build/biosys-bs4test --game,
NewGame.sav) against the independent statements of tests/bs4p6_oracle.py (written from the decompile of the eight functions).

For each of the eight pages (panelasav panelaoc panelaevo panelarf: the air lungs; panelwsav panelwoc panelwevo panelwrf: the water pumps):
  1. opened by real clicks from the main page that carries it (oxyair / watpur): the page, the opening hotspot, the highlight, the features
  2. the readouts and the items shown for many game states (idle, running to every destination, drawing, received by another lung / pump,
     power failure, a destination code the panel does not know): every gauge shows what the game returns for its parameter, the items,
     the overlay / broken pictures, the mirrors, the loop sound and the click sound follow the rule read from the init
  3. every routing control: a real click on each enabled button, up to three buttons deep; the set-parameter events sent are those of the
     setter rule, in order, the game then has the state, and after the panel is closed the route overlays of the main page follow the game
  4. the route checks (exe:004319d0 / 00431c30) for every combination of pump / lung states
  5. the once-per-second poll: a change made in the game is followed (mirror -> panel reload, receiving flag, destination gauge)
  6. one frame per panel: build/shots/bs4p6_<page>.bmp"""
import itertools
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bs4_oracle  # noqa: E402
import bs4main_oracle as MO  # noqa: E402  (only the table of the overlay ornaments, extracted from the exe)
import bs4p6_oracle as O  # noqa: E402

ROOT = os.path.dirname(HERE)
BIN = os.path.join(ROOT, 'build', 'biosys-bs4test')
SHOTS = os.path.join(ROOT, 'build', 'shots')
os.makedirs(SHOTS, exist_ok=True)

fails = 0
passes = 0
counts = {}


def check(ok, what, detail=''):
    global fails, passes
    if ok:
        passes += 1
        print('PASS ' + what)
    else:
        fails += 1
        print('FAIL ' + what + (': ' + str(detail)[:1200] if detail != '' else ''))


class Tally:
    """many small checks of one kind: one PASS / FAIL line"""

    def __init__(self, what):
        self.what = what
        self.n = 0
        self.bad = []

    def ok(self, cond, detail=''):
        self.n += 1
        if not cond:
            self.bad.append(str(detail))

    def done(self, min_n=1):
        check(not self.bad and self.n >= min_n, '%s (%d checks)' % (self.what, self.n),
              '%d bad, first: %s' % (len(self.bad), '; '.join(self.bad[:3])) if self.bad else 'only %d checks' % self.n)


def run(lines):
    r = subprocess.run([BIN, '--game'], input='\n'.join(lines) + '\n', capture_output=True, text=True)
    return r.stdout.splitlines()


def segments(out):
    seg, cur = {}, None
    for l in out:
        if l.startswith('@@ '):
            cur = l[3:]
            seg[cur] = []
        elif cur is not None:
            seg[cur].append(l)
    return seg


GET = re.compile(r'get (\S+) (\d+) = (ok|no) int=(-?\d+) dbl=(\S+)')


def gets_of(lines):
    g = {}
    for l in lines:
        m = GET.match(l)
        if m:
            g[(m.group(1), int(m.group(2)))] = (int(m.group(4)) & 0xffffffff, float(m.group(5))) if m.group(3) == 'ok' else None
    return g


def parse_p6(lines):
    for l in lines:
        if l.startswith('p6 '):
            d = dict(re.findall(r'(\w+)=(\S*)', l))
            d['mask'] = int(d['mask'], 16)
            for k in ('active', 'reload', 'loop', 'recv', 'draw', 'dest', 'overlay', 'broken'):
                if k in d:
                    d[k] = int(d[k])
            d['gauges'] = []
            for i in range(6):
                v, pct, pid, img = d['g%d' % i].split('/')
                d['gauges'].append((float(v), int(pct), int(pid, 16), int(img)))
            return d
    return None


def events_of(lines):
    ev = []
    for l in lines:
        m = re.match(r'set (\S+) (\d+) (-?\d+) (-?\d+) (-?\d+)$', l)
        if m:
            ev.append((m.group(1), int(m.group(2)), int(m.group(3))))
    return ev


def logs_of(lines):
    return [l[4:] for l in lines if l.startswith('log ')]


def loops_of(lines):
    return [l[5:] for l in lines if l.startswith('loop ')]


def clicks_of(lines):
    for l in lines:
        if l.startswith('clicks'):
            return [int(x) for x in l.split()[1:]]
    return []


def orn_of(lines):
    r = {}
    for l in lines:
        m = re.match(r'orn (\d+) (\S+) (\d+)$', l)
        if m:
            r[m.group(2)] = int(m.group(3))
    return r


def hot_of(lines):
    r = []
    for l in lines:
        m = re.match(r'phot (\d+) (\S+) (-?\d+) (-?\d+) (-?\d+) (-?\d+) flags=(\d+) verb=([0-9a-f]+) args=(-?\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+)', l)
        if m:
            a = [int(m.group(i)) & 0xffffffff for i in range(9, 15)]
            r.append(dict(name=m.group(2), enabled=not (int(m.group(7)) & 1), verb=int(m.group(8), 16), show=a[2], hide=a[5]))
    return r


def mask_of(lines):
    for l in lines:
        if l.startswith('mask panel '):
            return int(l.split()[2], 16)
    return None


def items(mask, dyn):
    return {i for i in dyn if mask >> i & 1}


# ---------------------------------------------------------------------------------------------------------------------------------------
ORACLE = bs4_oracle.Oracle()
TABLES = MO.Tables()
_exist = {}


def existing_items(name):
    """the items n (0..31) a panel page has at all: a hotspot or a type-1 ornament named `n_...` (spec 1.6)"""
    if name not in _exist:
        p = ORACLE.page(ORACLE.names.index(name) + 1)
        names = [h['name'] for h in p.hotspots] + [o['name'] for o in p.ornaments if o['type'] == 1]
        _exist[name] = {n for n in range(32) if any(x.startswith('%d_' % n) for x in names)}
    return _exist[name]


KINDS = ('air', 'water')


def pages(kind):
    return O.AIR_PAGE if kind == 'air' else O.WATER_PAGE


def open_lines(kind, b):
    m, t, h = (O.AIR_OPEN if kind == 'air' else O.WATER_OPEN)[b]
    return ['load ' + m, 'clickh ' + t, 'clickh ' + h]


def want_gets(kind, b):
    """every get the expectations need: all four devices' route state, the malfunction of this panel's device, the gauge parameters"""
    g = []
    if kind == 'air':
        for x in O.BIOMES:
            g += ['get %s %d' % (O.LUNG[x], i) for i in (2, 0x11, 0x14)]
        g.append('get %s 7' % O.LUNG[b])
        g += ['get %s %d' % (O.LUNG[b], pid) for (_, pid, _, _) in O.AIR_GAUGES]
    else:
        for x in O.BIOMES:
            g += ['get %s %d' % (O.PUMP[x], i) for i in (2, 0x14)]
        g.append('get %s 7' % O.TANK[b])
        g += ['get %s %d' % (O.TANK[b], pid) for pid in (0x2b, 0x2c, 0x28)]
        g += ['get %s 0x2b' % O.PUMP[x] for x in O.BIOMES]
    return g


def dev_states(kind, G):
    if kind == 'air':
        return {x: (G[(O.LUNG[x], 2)][0], G[(O.LUNG[x], 0x11)][0], G[(O.LUNG[x], 0x14)][0]) for x in O.BIOMES}
    return {x: (G[(O.PUMP[x], 2)][0], G[(O.PUMP[x], 0x14)][0]) for x in O.BIOMES}


def expect_init(kind, b, G):
    st = dev_states(kind, G)
    if kind == 'air':
        return O.air_init(b, G[(O.LUNG[b], 7)][0], st)
    return O.water_init(b, G[(O.TANK[b], 7)][0], st)


def check_panel_against(t, kind, b, P, G, logs, loops, clicks, tag):
    """the page state P (the p6 line) after an init against the rule"""
    dyn = O.DYN_AIR if kind == 'air' else O.DYN_WATER
    E = expect_init(kind, b, G)
    ex = existing_items(P['page'])
    t.ok(E['dyn'] is None or items(P['mask'], dyn) == E['dyn'] & ex, '%s items %s want %s' % (tag, sorted(items(P['mask'], dyn)), sorted((E['dyn'] or set()) & ex)))
    if 'overlay' in P:
        t.ok(P['overlay'] == (0 if E['overlay'] else 1), '%s overlay hidden flag %s' % (tag, P['overlay']))
    if 'broken' in P:
        t.ok(P['broken'] == (0 if E['broken'] else 1), '%s broken hidden flag %s' % (tag, P['broken']))
    if E['broken']:
        if clicks is not None:
            t.ok(clicks and clicks[-1] == 7, '%s broken: click sound 7 %s' % (tag, clicks))
        return E
    t.ok(P['recv'] == E['recv'] and P['dest'] == E['dest'] and (kind == 'water' or P['draw'] == E['draw']),
         '%s mirrors recv %d dest %d draw %d want %s' % (tag, P['recv'], P['dest'], P['draw'], E))
    if not E['ok']:
        t.ok(any('Unknown value' in l for l in logs) or kind == 'air' or kind == 'water', '%s error logged %s' % (tag, logs))
        return E
    t.ok(not logs, '%s no log: %s' % (tag, logs))
    t.ok(P['active'] == E['active'], '%s active mirror %d want %d' % (tag, P['active'], E['active']))
    t.ok(loops[-1:] == [('%d start' if E['active'] else '%d stop') % P['loop']], '%s loop sound %s' % (tag, loops))
    t.ok(P['loop'] == (1 if kind == 'air' else 5), '%s loop id %d' % (tag, P['loop']))
    if kind == 'air':
        gl = O.AIR_GAUGES
        L = O.LUNG[b]
        for (slot, pid, mn, mx) in gl:
            if slot == 5 and E['dest'] in (-1, 0x1a):
                v = 0.0
            else:
                v = O.clamp(G[(L, pid)][1], mn, mx)
            last, pct, gpid, img = P['gauges'][slot]
            t.ok(last == v and pct == O.gauge_percent(v, mn, mx) and gpid == pid and img >= 0,
                 '%s gauge %d: %s want %s/%d id %#x' % (tag, slot, P['gauges'][slot], v, O.gauge_percent(v, mn, mx), pid))
    else:
        T = O.TANK[b]
        for (slot, pid, mn, mx) in O.WATER_GAUGES:
            if slot == 3:
                v = 0.0 if E['dest'] == -1 else O.clamp(G[(O.PUMP[b], 0x2b)][1], mn, mx)
            else:
                v = O.clamp(G[(T, pid)][1], mn, mx)
            last, pct, gpid, img = P['gauges'][slot]
            t.ok(last == v and pct == O.gauge_percent(v, mn, mx) and gpid == pid and img >= 0,
                 '%s gauge %d: %s want %s/%d id %#x' % (tag, slot, P['gauges'][slot], v, O.gauge_percent(v, mn, mx), pid))
    return E


# ====================================================================================================================== 1. opening
def test_open():
    t = Tally('1. every panel opens by real clicks from its main page: page, opener, highlight, devices, loop')
    for kind in KINDS:
        for b in O.BIOMES:
            lines = ['get %s 7' % (O.LUNG[b] if kind == 'air' else O.TANK[b])] + open_lines(kind, b) + ['echo @@ st', 'state', 'p6', 'logs']
            out = run(lines)
            broken = list(gets_of(out).values())[0][0] != 0      # the dead biome's machines have no power in NewGame.sav: the panel is the broken one
            seg = segments(out)
            st = ' '.join(seg.get('st', []))
            P = parse_p6(seg.get('st', []))
            name = pages(kind)[b]
            m, tech, hot = (O.AIR_OPEN if kind == 'air' else O.WATER_OPEN)[b]
            t.ok('page=%s ' % m in st and 'panel=%s ' % name in st and 'opener=%s ' % hot in st, '%s: %s' % (name, st[:150]))
            hl = {('air', 's'): 'hiairts1/0', ('air', 'o'): 'hiairto1/0', ('air', 'e'): 'hiairte1/0', ('air', 'r'): 'hiairtr1/0',
                  ('water', 's'): 'hiutils1/14', ('water', 'o'): 'hiutilo1/14', ('water', 'e'): 'hiutile1/14', ('water', 'r'): 'hiutilr1/14'}[(kind, b)]
            t.ok('highlight=%s ' % hl in st, '%s highlight %s' % (name, st[:200]))
            t.ok(P is not None and P['page'] == name, '%s p6 %s' % (name, P))
            if P:
                loop = -1 if broken else (1 if kind == 'air' else 5)
                if kind == 'air':
                    t.ok(P['feature'] == O.LUNG[b] and P['feature2'] == '' and P['loop'] == loop, '%s features %s' % (name, P))
                else:
                    t.ok(P['feature'] == O.TANK[b] and P['feature2'] == O.PUMP[b] and P['loop'] == loop, '%s features %s' % (name, P))
            t.ok(not logs_of(seg.get('st', [])), '%s logs %s' % (name, logs_of(seg.get('st', []))))
    t.done(40)


# ====================================================================================================================== 2. readouts and items
def air_setups(b):
    L = O.LUNG[b]
    s = [('idle', []), ('route-set-inactive', ['setp %s 20 21' % L])]
    for d in (21, 22, 23, 24, 26):
        s.append(('active-first dest %d' % d, ['setp %s 2 1' % L, 'setp %s 20 %d' % (L, d)]))
        s.append(('dest-first dest %d' % d, ['setp %s 20 %d' % (L, d), 'setp %s 2 1' % L]))
    s.append(('draw', ['setp %s 17 1' % L, 'setp %s 2 1' % L]))
    s.append(('draw inactive', ['setp %s 17 1' % L]))
    for x in O.BIOMES:
        s.append(('received from %s' % x, ['setp %s 2 1' % O.LUNG[x], 'setp %s 20 %d' % (O.LUNG[x], O.CODE[b])]))
        s.append(('received from %s (dest first)' % x, ['setp %s 20 %d' % (O.LUNG[x], O.CODE[b]), 'setp %s 2 1' % O.LUNG[x]]))
    s.append(('power failure', ['power 0']))
    s.append(('unknown destination 25 (own, active first)', ['setp %s 2 1' % L, 'setp %s 20 25' % L]))
    # (a lung with an unknown mode that is then switched on used to crash the game logic (dll:10010a30 reads the NULL target cel): the port ends its stroke instead, see tests/test_bs4fixes.py; the panel is not driven there)
    return s


def water_setups(b):
    P = O.PUMP[b]
    s = [('idle', []), ('route-set-inactive', ['setp %s 20 22' % P])]
    for d in (21, 22, 23, 24):
        s.append(('active dest %d' % d, ['setp %s 20 %d' % (P, d), 'setp %s 2 1' % P]))
    for x in O.BIOMES:
        s.append(('received from %s' % x, ['setp %s 20 %d' % (O.PUMP[x], O.CODE[b]), 'setp %s 2 1' % O.PUMP[x]]))
    s.append(('power failure', ['power 0']))
    s.append(('unknown destination 25 (own)', ['setp %s 20 25' % P, 'setp %s 2 1' % P]))
    o = [x for x in O.BIOMES if x != b][0]
    s.append(('unknown destination 25 (other pump)', ['setp %s 20 25' % O.PUMP[o], 'setp %s 2 1' % O.PUMP[o]]))
    return s


def test_states():
    t = Tally('2. readouts, items, pictures, mirrors, sounds of every panel for many game states')
    seen_dyn = {}
    for kind in KINDS:
        for b in O.BIOMES:
            for label, setup in (air_setups(b) if kind == 'air' else water_setups(b)):
                lines = setup + want_gets(kind, b) + ['echo @@ b', 'clicks'] + open_lines(kind, b) + ['echo @@ st', 'state', 'p6', 'logs', 'loops', 'clicks']
                out = run(lines)
                seg = segments(out)
                G = gets_of(out)
                P = parse_p6(seg.get('st', []))
                tag = '%s [%s]' % (pages(kind)[b], label)
                if P is None:
                    t.ok(False, '%s: no panel / failed get: %s' % (tag, out[-3:]))
                    continue
                clicks = clicks_of(seg['st'])
                E = check_panel_against(t, kind, b, P, G, logs_of(seg['st']), loops_of(seg['st']), clicks, tag)
                key = (kind, E['broken'], E['recv'], E['active'], E.get('draw', 0), E['dest'], E['ok'])
                seen_dyn[key] = seen_dyn.get(key, 0) + 1
    t.done(1000)
    # the matrix reached every branch of the inits
    for kind in KINDS:
        ks = [k for k in seen_dyn if k[0] == kind]
        check(any(k[1] for k in ks) and any(k[2] for k in ks) and any(k[3] and k[5] != -1 for k in ks) and any(not k[3] and not k[2] and not k[1] for k in ks)
              and (kind == 'air' or any(not k[6] for k in ks)) and (kind == 'water' or any(k[4] for k in ks)),
              '2. %s inits: broken, received, running, idle, draw (air) and error branches all reached' % kind, sorted(ks))
        if kind == 'air':
            check({k[5] for k in ks if k[3] and not k[4]} >= {0x15, 0x16, 0x17, 0x18, 0x1a}, '2. air: a running lung shown for every destination code incl. 0x1a',
                  sorted({k[5] for k in ks}))
        else:
            check({k[5] for k in ks if k[3]} >= {0x15, 0x16, 0x17, 0x18}, '2. water: a running pump shown for every destination code', sorted({k[5] for k in ks}))


# ====================================================================================================================== 3. controls
def state_vector(kind, G):
    return tuple(sorted(dev_states(kind, G).items()))


def control_gets(kind):
    g = []
    if kind == 'air':
        for x in O.BIOMES:
            g += ['get %s %d' % (O.LUNG[x], i) for i in (2, 0x11, 0x14)]
    else:
        for x in O.BIOMES:
            g += ['get %s %d' % (O.PUMP[x], i) for i in (2, 0x14)]
    return g


def expected_overlay_hidden(kind, name, st):
    m = re.fullmatch(r'hi(?:air|util)([soer])([soer])on', name)
    x, y = m.group(1), m.group(2)
    if kind == 'air':
        a, dr, d = st[x]
        on = a != 0 and dr == 0 and 0x15 <= d <= 0x18 and d == O.CODE[y]
    else:
        a, d = st[x]
        on = a != 0 and 0x15 <= d <= 0x18 and d == O.CODE[y]
    return 0 if on else 1


COVER = set()


def click_run(kind, b, setup, prefix, hs, t, tag):
    """setup, open, the buttons of `prefix`, then click `name`: returns (mask after, state vector after, p6 after) after the checks"""
    pre = ['echo @@ pre', 'mask panel', 'p6', 'events', 'loops', 'clicks']
    name = hs['name']
    lines = setup + open_lines(kind, b) + ['clickh ' + n for n in prefix] + pre + ['clickh ' + name, 'echo @@ post', 'mask panel', 'p6', 'logs', 'events', 'loops'] + \
        ['echo @@ g'] + control_gets(kind) + ['get %s 0x1b' % O.LUNG[b]] * (kind == 'air') + \
        ['act 1b', 'advance 1000', 'echo @@ main', 'overlays'] + control_gets(kind)
    out = run(lines)
    seg = segments(out)
    if 'main' not in seg:
        t.ok(False, '%s click %s: run failed %s' % (tag, name, out[-3:]))
        return None
    old = mask_of(seg['pre'])
    exist = sum(1 << n for n in existing_items(pages(kind)[b]))
    new = ((old | hs['show']) & ~hs['hide']) & exist         # the mask the verb 0x17 of the hotspot leaves (the setter then changes more items)
    t.ok(not (hs['show'] & hs['hide']), '%s click %s: show and hide masks overlap' % (tag, name))
    P = parse_p6(seg['post'])
    ev = events_of(seg['post'])
    G = gets_of(seg['g'])
    Gm = gets_of(seg['main'])
    where = '%s %s click %s' % (pages(kind)[b], tag, name)
    want = (O.air_setter if kind == 'air' else O.water_setter)(b, new, old)
    t.ok(ev == want, '%s: events %s want %s (mask %#x -> %#x)' % (where, ev, want, old, new))
    t.ok(not logs_of(seg['post']), '%s: logs %s' % (where, logs_of(seg['post'])))
    for e in want:
        COVER.add((kind, e[1], e[2] if e[1] != 0x14 else 'dest'))
    # the game has the state the events describe
    f = O.LUNG[b] if kind == 'air' else O.PUMP[b]
    act = [e[2] for e in want if e[1] == 2]
    if act and act[-1] == 0:
        t.ok(G[(f, 2)][0] == 0, '%s: game active %s after turn off' % (where, G[(f, 2)]))
    dests = [e[2] for e in want if e[1] == 0x14]
    if dests:
        t.ok(G[(f, 0x14)][0] == dests[-1], '%s: game destination %s want %#x' % (where, G[(f, 0x14)], dests[-1]))
        if kind == 'air':
            t.ok(G[(f, 0x11)][0] == 0, '%s: game draw flag after a route' % where)
    if any(e[1] == 0x11 for e in want):
        t.ok(G[(f, 0x11)][0] == 1, '%s: game draw flag %s' % (where, G[(f, 0x11)]))
    # the page mirrors and the sounds
    broken = P['loop'] == -1                  # the init stopped before it chose the loop sound: a broken panel plays none (its controls still work)
    if act:
        t.ok(P['active'] == act[-1], '%s: active mirror %d' % (where, P['active']))
        t.ok(loops_of(seg['post'])[-1:] == ([] if broken else [('%d start' if act[-1] else '%d stop') % P['loop']]), '%s: loop sound %s' % (where, loops_of(seg['post'])))
    else:
        t.ok(not loops_of(seg['post']), '%s: loop sound without an active change: %s' % (where, loops_of(seg['post'])))
    if dests:
        shown = {}
        if kind == 'air':
            shown = O.air_after_route(dests[-1]) if want[-1][1] == 0x14 else {}
            if shown:
                t.ok(P['dest'] == dests[-1], '%s: dest mirror %d' % (where, P['dest']))
                gl = G[(f, 0x1b)][1] if dests[-1] != 0x1a else None
                if gl is not None and not broken:
                    last, pct, pid, img = P['gauges'][5]
                    v = O.clamp(gl, 0.0, 100.0)
                    t.ok(last == v and pct == O.gauge_percent(v, 0.0, 100.0), '%s: destination volume gauge %s want %s' % (where, P['gauges'][5], v))
        else:
            shown = {1: True, 2: False}
            t.ok(P['dest'] == dests[-1], '%s: dest mirror %d' % (where, P['dest']))
            gp = Gm.get((O.PUMP[b], 0x2b))
        for it, on in shown.items():
            t.ok(bool(P['mask'] >> it & 1) == on, '%s: item %#x should be %s after the route' % (where, it, on))
    # after the panel is closed the main page's overlays follow the game
    st = dev_states(kind, Gm)
    ovs = {}
    for l in seg['main']:
        if l.startswith('overlays'):
            ovs = {int(i): int(v) for i, v in re.findall(r'(\d+)=(-?\d+)', l)}
    rows = range(79, 91) if kind == 'air' else range(91, 103)
    for k in rows:
        n = TABLES.overlays[k]['ornament']
        want = 1 - expected_overlay_hidden(kind, n, st)
        t.ok(ovs.get(k, 0) == want, '%s: overlay %d %s state %d want %d (states %s)' % (where, k, n, ovs.get(k, 0), want, st))
    return new, state_vector(kind, Gm), P


def enabled_controls(kind, b, setup, prefix):
    lines = setup + open_lines(kind, b) + ['clickh ' + n for n in prefix] + ['echo @@ h', 'phot']
    seg = segments(run(lines))
    names = {}
    for h in hot_of(seg.get('h', [])):
        if h['enabled'] and h['verb'] == 0x17 and h['name'] not in names:
            names[h['name']] = h
    return names


def walk(kind, b, setup, tag, t, budget):
    seen = set()
    queue = [[]]
    n = 0
    while queue and n < budget:
        prefix = queue.pop(0)
        for name, hs in enabled_controls(kind, b, setup, prefix).items():
            r = click_run(kind, b, setup, prefix, hs, t, tag + ' ' + '>'.join(prefix))
            n += 1
            if r is None:
                continue
            key = (r[0], r[1])
            if key not in seen and len(prefix) < 2:
                seen.add(key)
                queue.append(prefix + [name])
    return n


def test_controls():
    t = Tally('3. every routing control: real clicks send the setter rule\'s events in order, the game applies them, the overlays follow')
    total = 0
    for kind in KINDS:
        for b in O.BIOMES:
            L = O.LUNG[b] if kind == 'air' else O.PUMP[b]
            seeds = [('idle', [])]
            if kind == 'air':
                seeds += [('running', ['setp %s 2 1' % L]), ('drawing', ['setp %s 17 1' % L, 'setp %s 2 1' % L])]
            else:
                seeds += [('running', ['setp %s 2 1' % L])]
            for label, setup in seeds:
                total += walk(kind, b, setup, '[%s]' % label, t, 40)
    t.done(1500)
    air = {c for c in COVER if c[0] == 'air'}
    water = {c for c in COVER if c[0] == 'water'}
    check({('air', 2, 0), ('air', 2, 1), ('air', 0x11, 1), ('air', 0x14, 'dest')} <= air,
          '3. air: the clicks reached turn off, turn on, draw and the routes (%d buttons clicked in all)' % total, sorted(air))
    check({('water', 2, 0), ('water', 2, 1), ('water', 0x14, 'dest')} <= water, '3. water: the clicks reached turn off, turn on and the routes', sorted(water))


# ====================================================================================================================== 4. route checks
def test_routes():
    t = Tally('4. the route checks of exe:004319d0 / 00431c30 for every combination of pump / lung states')
    # pumps: idle, running to each of the four destinations, running to an unknown code
    pstates = [('idle', lambda f: []), ] + [('run %d' % d, (lambda d: lambda f: ['setp %s 20 %d' % (f, d), 'setp %s 2 1' % f])(d)) for d in (21, 22, 23, 24, 25)]
    lines = []
    combos = list(itertools.product(range(len(pstates)), repeat=4))
    for c in combos:
        lines += ['echo @@ c']
        for x, si in zip(O.BIOMES, c):
            f = O.PUMP[x]
            lines += ['setp %s 2 0' % f, 'setp %s 20 22' % f] + pstates[si][1](f)
            lines += ['get %s 2' % f, 'get %s 20' % f]
        for x in O.BIOMES:
            lines.append('p6route pump %s' % O.TANK[x])
    lines.append('p6route pump fbx9tank')
    out = run(lines)
    chunks = out_split(out)
    t.ok(len(chunks) == len(combos), 'pump chunks %d want %d' % (len(chunks), len(combos)))
    seen = {0: 0, 1: 0, None: 0}
    for c, ch in zip(combos, chunks):
        G = gets_of(ch)
        st = {x: (G[(O.PUMP[x], 2)][0], G[(O.PUMP[x], 0x14)][0]) for x in O.BIOMES}
        res = [r for r in ch if r.startswith('p6route')]
        for x, line in zip(O.BIOMES, res):
            want = O.pump_receiving(x, st)
            got = None if ' fail ' in line else int(line.split('out=')[1])
            t.ok(got == want, 'pumps %s tank %s: %r want %r' % (st, O.TANK[x], line, want))
            seen[want] += 1
    check(all(seen.values()), '4. pump check: true, false and failure all occurred (%s)' % seen)
    t.ok(any(l.startswith('p6route pump fbx9tank fail') for l in out), 'an unknown tank name fails')
    # lungs: idle, running to each of the four destinations and the own biome (0x1a), drawing, running to an unknown code
    lstates = [('idle', lambda f: [])] + [('run %d' % d, (lambda d: lambda f: ['setp %s 2 1' % f, 'setp %s 20 %d' % (f, d)])(d)) for d in (21, 22, 23, 24, 26, 25)] + \
        [('draw', lambda f: ['setp %s 17 1' % f, 'setp %s 2 1' % f])]
    lines = []
    combos = list(itertools.product(range(len(lstates)), repeat=4))
    for c in combos:
        lines += ['echo @@ c']
        for x, si in zip(O.BIOMES, c):
            f = O.LUNG[x]
            lines += ['setp %s 2 0' % f, 'setp %s 20 26' % f] + lstates[si][1](f)
            lines += ['get %s 2' % f, 'get %s 17' % f, 'get %s 20' % f]
        for x in O.BIOMES:
            lines.append('p6route lung %s' % O.LUNG[x])
    lines.append('p6route lung fbx9lung')
    out = run(lines)
    chunks = out_split(out)
    t.ok(len(chunks) == len(combos), 'lung chunks %d want %d' % (len(chunks), len(combos)))
    seen = {0: 0, 1: 0, None: 0}
    for c, ch in zip(combos, chunks):
        G = gets_of(ch)
        st = {x: (G[(O.LUNG[x], 2)][0], G[(O.LUNG[x], 0x11)][0], G[(O.LUNG[x], 0x14)][0]) for x in O.BIOMES}
        res = [r for r in ch if r.startswith('p6route')]
        for x, line in zip(O.BIOMES, res):
            want = O.lung_receiving(x, st)
            got = None if ' fail ' in line else int(line.split('out=')[1])
            t.ok(got == want, 'lungs %s lung %s: %r want %r' % (st, O.LUNG[x], line, want))
            seen[want] += 1
    check(seen[0] and seen[1], '4. lung check: true and false occurred (%s; a lung state that fails the check cannot be made in the game)' % seen)
    t.ok(any(l.startswith('p6route lung fbx9lung fail') for l in out), 'an unknown lung name fails')
    t.done(10000)


def out_split(out):
    chunks, cur = [], None
    for l in out:
        if l.startswith('@@ c'):
            cur = []
            chunks.append(cur)
        elif cur is not None:
            cur.append(l)
    return chunks


# ====================================================================================================================== 5. poll
def test_poll():
    t = Tally('5. the once-per-second poll follows changes made in the game')
    for kind in KINDS:
        for b in O.BIOMES:
            dev = O.LUNG[b] if kind == 'air' else O.PUMP[b]
            name = pages(kind)[b]
            other = [x for x in O.BIOMES if x != b][0]
            od = O.LUNG[other] if kind == 'air' else O.PUMP[other]
            mal = gets_of(run(['get %s 7' % (O.LUNG[b] if kind == 'air' else O.TANK[b])]))
            if list(mal.values())[0][0]:
                # the dead biome's machines have no power in NewGame.sav: the broken panel has no handler, nothing polls, nothing reloads
                out = run(open_lines(kind, b) + ['advance 3000', 'echo @@ a', 'p6'])
                Pa = parse_p6(segments(out)['a'])
                t.ok(Pa['broken'] == 0 and Pa['reload'] == 0 and Pa['loop'] == -1, '%s broken stays broken: %s' % (name, Pa))
                continue
            # (a) the running device is switched off in the game: the mirror differs, the panel reloads and shows the idle state
            lines = ['setp %s 2 1' % dev] + open_lines(kind, b) + ['echo @@ a', 'p6', 'loops', 'advance 300', 'echo @@ a2', 'p6', 'setp %s 2 0' % dev, 'advance 1200',
                                                                       'echo @@ b', 'p6', 'logs', 'loops'] + want_gets(kind, b)
            out = run(lines)
            seg = segments(out)
            Pa, Pa2, Pb = parse_p6(seg['a']), parse_p6(seg['a2']), parse_p6(seg['b'])
            G = gets_of(out)
            t.ok(Pa['active'] == 1 and Pa2['reload'] == 0, '%s (a) running before: %s' % (name, Pa))
            t.ok(Pb['active'] == 0 and Pb['reload'] == 0, '%s (a) switched off in the game: the panel was reloaded: %s' % (name, Pb))
            check_panel_against(t, kind, b, Pb, G, logs_of(seg['b']), loops_of(seg['b']), None, '%s (a)' % name)
            # (b) received by another device, which is then switched off: the overlay goes, the route buttons come
            recv_setup = ['setp %s 2 1' % od, 'setp %s 20 %d' % (od, O.CODE[b])] if kind == 'air' else ['setp %s 20 %d' % (od, O.CODE[b]), 'setp %s 2 1' % od]       # (a lung that is switched on first stays on when its route changes)
            lines = recv_setup + open_lines(kind, b) + ['echo @@ a', 'p6', 'advance 1200', 'echo @@ a2', 'p6', 'setp %s 2 0' % od, 'advance 1200', 'echo @@ b', 'p6', 'logs', 'loops'] + want_gets(kind, b)
            out = run(lines)
            seg = segments(out)
            Pa, Pa2, Pb = parse_p6(seg['a']), parse_p6(seg['a2']), parse_p6(seg['b'])
            G = gets_of(out)
            t.ok(Pa['recv'] == 1 and Pa2['recv'] == 1 and Pa2['reload'] == 0, '%s (b) received: %s' % (name, Pa))
            t.ok(Pb['recv'] == 0 and Pb['reload'] == 0 and not Pb['overlay'] == 0, '%s (b) no longer received: %s' % (name, Pb))
            check_panel_against(t, kind, b, Pb, G, logs_of(seg['b']), loops_of(seg['b']), None, '%s (b)' % name)
            # (c) the destination gauge follows the game
            if kind == 'air':
                lines = ['setp %s 2 1' % dev, 'setp %s 20 %d' % (dev, O.CODE[other])]
                chg = 'setp %s 20 %d' % (dev, O.CODE[[x for x in O.BIOMES if x not in (b, other)][0]])
                gq = 'get %s 0x1b' % dev
                slot = 5
            else:
                lines = ['setp %s 20 %d' % (dev, O.CODE[other]), 'setp %s 2 1' % dev]
                chg = 'setp %s 20 %d' % (dev, O.CODE[[x for x in O.BIOMES if x not in (b, other)][0]])
                gq = 'get %s 0x2b' % dev
                slot = 3
            tank_a = O.GAME_TANK[O.CODE[other]]
            tank_b = O.GAME_TANK[O.CODE[[x for x in O.BIOMES if x not in (b, other)][0]]]
            lines += ['tank %s 20' % tank_a, 'tank %s 80' % tank_b] if kind == 'water' else []
            lines += open_lines(kind, b) + ['echo @@ a', 'p6', gq, chg, 'advance 1200', 'echo @@ b', 'p6', gq, 'logs']
            out = run(lines)
            seg = segments(out)
            Pa, Pb = parse_p6(seg['a']), parse_p6(seg['b'])
            ga, gb = gets_of(seg['a']), gets_of(seg['b'])
            va = ga[(dev, 0x1b if kind == 'air' else 0x2b)][1]
            vb = gb[(dev, 0x1b if kind == 'air' else 0x2b)][1]
            if Pa['active'] == 1 and Pa['dest'] != -1 and Pa['dest'] != 0x1a:
                t.ok(Pa['gauges'][slot][0] == va, '%s (c) before: gauge %s game %s' % (name, Pa['gauges'][slot], va))
                t.ok(abs(va - vb) >= 0.1, '%s (c) the game value changed %s -> %s' % (name, va, vb))
                t.ok(Pb['gauges'][slot][0] == vb and Pb['gauges'][slot][1] == O.gauge_percent(vb, 0.0, 100.0), '%s (c) after the poll: gauge %s game %s' % (name, Pb['gauges'][slot], vb))
            else:
                t.ok(False, '%s (c) device did not run to the destination: %s' % (name, Pa))
    t.done(100)


# ====================================================================================================================== 6. frames
def test_shots():
    for kind in KINDS:
        for b in O.BIOMES:
            name = pages(kind)[b]
            f = O.LUNG[b] if kind == 'air' else O.PUMP[b]
            path = os.path.join(SHOTS, 'bs4p6_%s.bmp' % name)
            if os.path.exists(path):
                os.remove(path)
            setup = ['setp %s 2 1' % f, 'setp %s 20 26' % f] if kind == 'air' else ['setp %s 20 %d' % (f, O.CODE['o' if b != 'o' else 's']), 'setp %s 2 1' % f]
            run(setup + open_lines(kind, b) + ['advance 1000', 'shot ' + path])
            check(os.path.exists(path) and os.path.getsize(path) > 300000, '6. frame %s written' % os.path.relpath(path, ROOT))


only = os.environ.get('P6_ONLY', '').split(',')      # e.g. P6_ONLY=open,states while developing
for name, fn in (('open', test_open), ('states', test_states), ('controls', test_controls), ('routes', test_routes), ('poll', test_poll), ('shots', test_shots)):
    if only == [''] or name in only:
        fn()
print('%d passed, %d failed' % (passes, fails))
print('ALL OK' if not fails else '%d FAILED' % fails)
sys.exit(1 if fails else 0)
