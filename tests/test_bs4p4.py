#!/usr/bin/env python3
"""BioSys 4 device panels A (packet P4; src/engine/bs4panel_{sched,range,fans,gauges}.cpp, bs4panels_a.cpp): the pages 54 panelcfan, 55 panelsfan,
56 panelheat, 57 panelrain, 59 panelmist, 60 panelvent, 64 panelsump, 65 panelosm, 66 paneluv, 67 panelfilt, 68 paneltide, 69 panelwave.
Driven through build/biosys-bs4test with the real game logic (--game, NewGame.sav): every panel (every variant) is opened by real clicks on the
hotspots of the main page that carries it; readouts, gauges, items, sliders and buttons are compared with the values the game returns for the
feature parameters. The widget state is read through the driver command `p4` (bs4panels_a.cpp debugState). Frames: build/shots/bs4p4_*.bmp"""
import os
import re
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bs4_oracle  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, 'build', 'biosys-bs4test')
SHOTS = os.path.join(ROOT, 'build', 'shots')
os.makedirs(SHOTS, exist_ok=True)

fails = 0
npass = 0


def check(ok, what, detail=''):
    global fails, npass
    if ok:
        npass += 1
        print('PASS ' + what)
    else:
        fails += 1
        print('FAIL ' + what + (' -- ' + str(detail)[:1500] if detail != '' else ''))


oracle = bs4_oracle.Oracle()
names = oracle.names


def run(lines, game=True):
    p = subprocess.run([BIN] + (['--game'] if game else []), input='\n'.join(lines) + '\n', capture_output=True, text=True, timeout=600)
    return [l for l in p.stdout.splitlines() if l not in ('clip', 'unclip')]


# ------------------------------------------------------------------------------------------------------------ geometry of the main pages
class Sim:
    """the hotspots of a terminal page that are enabled at load, and the verb-0x17 item groups (spec 1.6 / 1.7)"""

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


_paths = {}


def find_path(panel, opener):
    """(main page name, [script lines that open the panel by clicks], (panel x, panel y))"""
    key = (panel, opener)
    if key in _paths:
        return _paths[key]
    pn = names.index(panel) + 1
    res = None
    for n in range(1, 46):
        s = Sim(n)
        for hi, h in enumerate(s.p.hotspots):
            if h['action'][0] != 0x1a or h['action'][1] != pn or h['name'] != opener:
                continue
            xy = (h['action'][2], h['action'][3])
            if s.en[hi]:
                pt = s.point(hi)
                if pt:
                    res = (names[n - 1], ['click %d %d' % pt], xy)
                    break
                continue
            item = int(re.match(r'(\d+)_', opener).group(1))
            for gi, g in enumerate(s.p.hotspots):
                if g['action'][0] == 0x17 and s.en[gi] and (g['action'][1] >> item) & 1:
                    gp = s.point(gi)
                    s2 = Sim(n)
                    s2.apply17(g)
                    hp = s2.point(hi) if s2.en[hi] else None
                    if gp and hp:
                        res = (names[n - 1], ['click %d %d' % gp, 'click %d %d' % hp], xy)
                        break
            if res:
                break
        if res:
            break
    _paths[key] = res
    return res


def open_lines(panel, opener):
    main, clicks, xy = find_path(panel, opener)
    return ['load %s' % main] + clicks, xy


_items = {}


def has_item(panel, n):
    if (panel, n) not in _items:
        p = oracle.page(names.index(panel) + 1)
        pre = '%d_' % n
        _items[(panel, n)] = any(h['name'].startswith(pre) for h in p.hotspots) or any(o['name'].startswith(pre) for o in p.ornaments)
    return _items[(panel, n)]


# ------------------------------------------------------------------------------------------------------------ what the panels are
def V(prefix, tail, feats):
    return dict(('%s_%s%s' % (prefix, tail[0], tail[1]), f) for tail, f in feats)


SPEC = {}


def spec(panel, kind, weight, loop, variants, **kw):
    SPEC[panel] = dict(kind=kind, weight=weight, loop=loop, variants=variants, **kw)


spec('panelcfan', 'on', 14, 2, [
    ('14_cfans1', 'fs9mixer'), ('14_cfans2', 'fs3mixer'), ('14_cfans3', 'fs7mixer'), ('14_cfano1', 'fomixer1'), ('14_cfano2', 'fomixer2'),
    ('14_cfano3', 'fomixer3'), ('14_cfane1', 'fdbmixer1'), ('14_cfane2', 'fdbmixer2'), ('14_cfane3', 'fdbmixer3'), ('14_cfanr1', 'fr9mixer'),
    ('14_cfanr2', 'fr5mixer'), ('14_cfanr3', 'fr2mixer')], sched=True, offbtn='2_activate', onbtn='1_stop')
spec('panelsfan', 'active', 2, 2, [
    ('2_soils1', 'fs1fan'), ('2_soils2', 'fs4fan'), ('2_soils3', 'fs7fan'), ('2_soile1', 'fdb6fan'), ('2_soile2', 'fdb3fan'),
    ('2_soile3', 'fdbfan3'), ('2_soilr1', 'fr8fan'), ('2_soilr2', 'fr3fan'), ('2_soilr3', 'fr10fan')],
    bars=[('airbar', 0x50, 0.0, 10.0)], offbtn='1_activate', onbtn='2_stop')
spec('panelheat', 'on', 4, 3, [
    ('4_heats1', 'fsheater'), ('4_heato1', 'foheater'), ('4_heate1', 'fdbheater'), ('4_heatr1', 'frfheater')],
    offbtn='2_turnon', onbtn='1_turnoff')
spec('panelrain', 'on', 6, 7, [
    ('6_rains1', 'fs2rain'), ('6_rains2', 'fs5rain'), ('6_rains3', 'fs8rain'), ('6_raino1', 'fo1rain'), ('6_raine1', 'fdbrain1'),
    ('6_raine2', 'fdbrain2'), ('6_raine3', 'fdbrain3'), ('6_rainr1', 'fr9rain'), ('6_rainr2', 'fr6rain'), ('6_rainr3', 'fr1rain')],
    sched=True, water=True, offbtn='2_turnon', onbtn='1_turnoff')
spec('panelmist', 'on', 1, 1, [('1_mistr1', 'fr9mist'), ('1_mistr2', 'fr5mist'), ('1_mistr3', 'fr1mist')], water=True,
     offbtn='2_turnon', onbtn='1_turnoff')
spec('panelvent', 'none', 1, 6, [
    ('1_vents1', 'fs5vent'), ('1_vents2', 'fs8vent'), ('1_vento1', 'fovent'), ('1_vente1', 'fdbvent1'), ('1_vente2', 'fdbvent2'),
    ('1_ventr1', 'fr7vent'), ('1_ventr2', 'fr6vent')], bars=[('airbar', 0x50, 0.0, 10.0)])
spec('panelsump', 'none', 0, -1, [
    ('0_sumps1', 'fbsdrain1'), ('0_sumps2', 'fbsdrain2'), ('0_sumps3', 'fbsdrain3'), ('0_sumpe1', 'fbddrain1'), ('0_sumpe2', 'fbddrain2'),
    ('0_sumpe3', 'fbddrain3'), ('0_sumpr1', 'fbrdrain1'), ('0_sumpr2', 'fbrdrain2'), ('0_sumpr3', 'fbrdrain3')],
    bars=[('waterbar', 0x51, 0.0, 10.0), ('siltbar', 0x52, 0.0, 100.0)], alarms=0x1000000, tier=True)
spec('panelosm', 'active', 2, 5, [('2_oss1', 'fbs4revos'), ('2_oso1', 'fbo4revos'), ('2_ose1', 'fbd3revos'), ('2_osr1', 'fbr4revos')],
     bars=[('osmbar', 0x2b, 0.0, 100.0), ('waterbar', 0x2c, 0.0, 100.0)], alarms=0x8000, inactive1=True, offbtn='1_activate', onbtn='2_stop')
spec('paneluv', 'active', 3, 3, [('3_uvs1', 'fbs4UV'), ('3_uvo1', 'fbo3UV'), ('3_uve1', 'fbd2UV'), ('3_uvr1', 'fbr3UV'), ('3_uvho1', 'fbp1UV')],
     bars=[('uvpbar', 0x2b, 0.0, 100.0), ('waterbar', 0x2c, 0.0, 100.0)], alarms=0x8000, inactive1=True, offbtn='1_activate', onbtn='2_stop',
     highlights={'3_uvho1': 'hiuvh1'})
spec('panelfilt', 'on', 9, 3, [
    ('9_fils1', 'fbs5scrub'), ('9_fils2', 'fbs3scrub'), ('9_fils3', 'fbs2scrub'), ('9_file1', 'fbd1scrub'), ('9_file2', 'fbd4scrub'),
    ('9_file3', 'fbd5scrub'), ('9_filr1', 'fbr5scrub'), ('9_filr2', 'fbr2scrub'), ('9_filr3', 'fbr1scrub')],
    sched=True, offbtn='2_turnon', onbtn='1_turnoff')
spec('paneltide', 'active', 10, 5, [('10_tideo1', 'fbo1tide')], bars=[('waterbar', 0x2b, 0.0, 100.0)], offbtn='2_turnon', onbtn='1_turnoff',
     highlights={'10_tideo1': 'hitide'}, noreload=True)
spec('panelwave', 'active', 11, 5, [('11_waveo1', 'fbo1wave')], offbtn='2_turnon', onbtn='1_turnoff', highlights={'11_waveo1': 'hiwave'},
     noreload=True)


# ------------------------------------------------------------------------------------------------------------ reading the driver output
def parse_kv(line):
    d = {}
    for tok in line.split(' ')[1:]:
        if '=' in tok:
            k, v = tok.split('=', 1)
            d[k] = v
    return d


def rect(s):
    return tuple(int(x) for x in s.split(','))


def parse_block(lines):
    """-> dict with 'p4' (kv), 'state' (kv), 'G' {(feature, id): (int, float)}, 'logs' [..], 'events' [..]"""
    out = {'p4': None, 'state': None, 'G': {}, 'logs': [], 'events': [], 'p4s': []}
    for l in lines:
        if l.startswith('p4 '):
            out['p4'] = parse_kv(l)
            out['p4s'].append(out['p4'])
        elif l.startswith('state '):
            out['state'] = parse_kv(l)
        elif l.startswith('get '):
            m = re.match(r'get (\S+) (\d+) = ok int=(-?\d+) dbl=(\S+)', l)
            if m:
                out['G'][(m.group(1), int(m.group(2)))] = (int(m.group(3)), float(m.group(4)))
        elif l.startswith('log '):
            out['logs'].append(l)
        elif l.startswith('set '):
            out['events'].append(l)
        elif l == '## act':
            out['events'] = []
    return out


def split_blocks(lines):
    blocks, cur, tag = {}, [], None
    for l in lines:
        if l.startswith('@@ '):
            if tag is not None:
                blocks[tag] = cur
            tag, cur = l[3:], []
        else:
            cur.append(l)
    if tag is not None:
        blocks[tag] = cur
    return blocks


def hm(sec):
    return '%d:%02d' % (sec // 3600, (sec % 3600) // 60)


def gauge_percent(v, mn, mx):
    return int(((v - mn) * 100.0 / (mx - mn)) * 151.0 * 0.01)


def clampv(v, hi):
    return min(max(v, 0.0), hi)


def marker_x(h, m):
    r = m % 10
    m = m - r if r + 5 < 10 else m - r + 10
    return m // 10 + (h * 3 + 12) * 2


def dbits(v):
    """(low, high) dwords of a double as the driver prints them (event 26 passes them as ints)"""
    b = struct.unpack('<Q', struct.pack('<d', v))[0]
    lo, hi = b & 0xffffffff, b >> 32
    return (lo - (1 << 32) if lo >= 1 << 31 else lo), (hi - (1 << 32) if hi >= 1 << 31 else hi)


SCALE_HEAT = struct.unpack('<d', struct.pack('<Q', 0x3fd1c71c71c71c72))[0]
SCALE_MIST = struct.unpack('<d', struct.pack('<Q', 0x3fe638e38e38e38e))[0]

GET_IDS = {'on': [7, 1, 2], 'active': [7, 2], 'none': [7]}


def get_ids(panel, sp):
    ids = set(GET_IDS[sp['kind']])
    if sp.get('sched'):
        ids |= {3, 6}
    if panel == 'panelsfan':
        ids |= {5, 0x50}
    if panel == 'panelvent':
        ids |= {0x50}
    if panel == 'panelheat':
        ids |= {100, 101, 102, 103}
    if panel == 'panelmist':
        ids |= {0x6e, 0x6f}
    if sp.get('water'):
        ids |= {0x5f}
    for b in sp.get('bars', []):
        ids.add(b[1])
    if sp.get('alarms'):
        ids.add(0x32)
    return sorted(ids)


# ------------------------------------------------------------------------------------------------------------ expectations
def check_state(panel, opener, feat, B, G, active_override=None):
    """-> list of mismatches between the widget state `B['p4']` and what the game said (G) for the panel after open / reload"""
    sp = SPEC[panel]
    p4 = B['p4']
    bad = []

    def eq(what, got, want):
        if got != want:
            bad.append('%s: %r != %r' % (what, got, want))

    def g(id, k=0):
        return G[(feat, id)][k]

    eq('page', p4['page'], panel)
    eq('feature', p4['feature'], feat)
    eq('reloadname', p4['reloadname'], '-' if sp.get('noreload') else opener)
    mask = int(p4['mask'], 16)

    def bit(n):
        return (mask >> n) & 1

    broken = g(7) != 0
    eq('broken ornament', int(p4['brokenorn']), 1 if broken else 0)
    eq('handler', int(p4['hdl']), 0 if broken else 1)
    if broken:
        eq('loop of a broken panel (the Init ends before it)', int(p4['loop']), -1)
        return bad
    eq('loop', int(p4['loop']), sp['loop'])
    kind = sp['kind']
    if kind == 'on':
        on = g(1) != 0
        eq('devOn', int(p4['on']), 1 if on else 0)
        if has_item(panel, 1):
            eq('item 1', bit(1), 1 if on else 0)
        if has_item(panel, 2):
            eq('item 2', bit(2), 0 if on else 1)
        active = g(2) != 0 if (feat, 2) in G else None
        if active is not None:
            eq('devActive', int(p4['active']), 1 if active else 0)
            if has_item(panel, 25):
                eq('item 0x19', bit(25), 1 if active else 0)
        if sp.get('sched'):
            on_s, len_s = g(3), g(6)
            off_s = len_s + on_s
            if off_s > 86400:
                off_s -= 86400
            eq('schedule', p4['sched'], '%s-%s' % (hm(on_s), hm(off_s)))
            mon = rect(p4['markOn'])
            moff = rect(p4['markOff'])
            if on:
                eq('on marker', (mon[0], mon[1]), (marker_x(on_s // 3600, (on_s % 3600) // 60), 0x51))
                eq('off marker', (moff[0], moff[1]), (marker_x(off_s // 3600, (off_s % 3600) // 60), 0x51))
            else:
                eq('markers hidden', (mon, moff), ((-1,) * 4, (-1,) * 4))
        if panel == 'panelheat':
            a, b = [float(x) for x in p4['heatday'].split(',')[:2]], None
            for key, ids in (('heatday', (100, 101)), ('heatnight', (102, 103))):
                f = p4[key].split(',')
                va, vb = clampv(g(ids[0], 1), 40.0), clampv(g(ids[1], 1), 40.0)
                eq(key + ' values', (float(f[0]), float(f[1])), (round(va, 4), round(vb, 4)))
                if on:
                    scale = SCALE_HEAT
                    row = 0x51 if key == 'heatday' else 0xa8
                    eq(key + ' markers', (int(f[2]), int(f[3]), int(f[6]), int(f[7])),
                       (int(va / scale) + 0x18, row, int(vb / scale) + 0x18, row))
        if panel == 'panelmist':
            f = p4['mist'].split(',')
            va, vb = clampv(g(0x6e, 1), 100.0), clampv(g(0x6f, 1), 100.0)
            eq('mist values', (float(f[0]), float(f[1])), (round(va, 4), round(vb, 4)))
            if on:
                eq('mist markers', (int(f[2]), int(f[3]), int(f[6]), int(f[7])),
                   (int(va / SCALE_MIST) + 0x18, 0x51, int(vb / SCALE_MIST) + 0x18, 0x51))
        if sp.get('water'):
            ws = g(0x5f)
            eq('water source', int(p4['ws'], 16), ws)
            eq('item 0x11', bit(0x11), 0 if ws == 0x60 else 1)
            eq('item 0x12', bit(0x12), 1 if ws == 0x60 else 0)
    elif kind == 'active':
        active = g(2) != 0
        eq('devActive', int(p4['active']), 1 if active else 0)
        if panel == 'panelsfan':
            tl = g(5)
            eq('time limit', p4['time'], '%d:%d' % (tl // 3600, (tl % 3600) // 60))
            eq('item 2', bit(2), 1 if active else 0)
            eq('item 1', bit(1), 1 if (not active and tl != 0) else 0)
            eq('item 0x1f', bit(31), 1 if (not active and tl == 0) else 0)
            tm = rect(p4['marker'])
            if not active or True:
                eq('time marker', (tm[0], tm[1]), (marker_x(tl // 3600, (tl % 3600) // 60), 0x7e))
        else:
            first = 1 if sp.get('inactive1') else 2
            on_item = 2 if sp.get('inactive1') else 1
            eq('item 1', bit(1), 1 if (active == (on_item == 1)) else 0)
            eq('item 2', bit(2), 1 if (active == (on_item == 2)) else 0)
        if has_item(panel, 25):
            eq('item 0x19', bit(25), 1 if active else 0)
    for gi, (bar, pid, mn, mx) in enumerate(sp.get('bars', [])):
        v = clampv(g(pid, 1), mx)
        got = p4['g%d' % gi].split('/')
        eq('gauge %d value' % gi, float(got[0]), round(v, 4))
        eq('gauge %d percent' % gi, int(got[1]), gauge_percent(v, mn, mx))
        eq('gauge %d param' % gi, int(got[2]), pid)
        if int(got[3]) < 0:
            bad.append('gauge %d has no bar image' % gi)
    if sp.get('alarms'):
        eq('alarm bits', int(p4['alarm'], 16), g(0x32))
    if sp.get('tier'):
        t = int(clampv(g(0x52, 1), 100.0))
        want = '0to9' if t < 10 else '10to20' if t < 0x15 else '21to40' if t < 0x29 else '41to60' if t < 0x3d else '81plus' if t >= 0x51 else '61to80'
        eq('tier', p4['tier'], want)
    return bad


def check_bars(panel, opener, B, bmp, xy):
    """the bar pixels of the gauges on the frame: `percent` columns of the bar image, the rest of the 151 columns cleared"""
    sp = SPEC[panel]
    bad = []
    w, h, rows = bs4_oracle.read_bmp(bmp)
    p = oracle.page(names.index(panel) + 1)
    grey = oracle.pal.index((206, 206, 206))
    for gi, (bar, pid, mn, mx) in enumerate(sp.get('bars', [])):
        oi = [i for i, o in enumerate(p.ornaments) if o['name'] == bar][0]
        o = p.ornaments[oi]
        iw, ih, stride, raw = p.image_pixels(o['image'])
        pct = int(B['p4']['g%d' % gi].split('/')[1])
        for r in range(4):
            src = raw[(ih - 1 - r) * stride:(ih - 1 - r) * stride + iw]
            for c in range(151):
                want = src[c] if c < pct else grey
                if rows[xy[1] + o['y'] + r][xy[0] + o['x'] + c] != want:
                    bad.append('%s bar pixel (%d,%d)' % (bar, c, r))
                    break
    return bad


# ------------------------------------------------------------------------------------------------------------ 1 + 2: open, readouts
def hi_name(panel, opener):
    sp = SPEC[panel]
    if opener in sp.get('highlights', {}):
        return sp['highlights'][opener]
    tail = opener.split('_', 1)[1]
    if panel == 'panelosm':                  # 2_oss1 -> hiosms1
        return 'hiosm' + tail[2:]
    if panel == 'panelfilt':                 # 9_fils1 -> hifilts1
        return 'hifilt' + tail[3:]
    return 'hi' + tail


for panel, sp in SPEC.items():
    ids = get_ids(panel, sp)
    script = []
    paths = {}
    for opener, feat in sp['variants']:
        lines, xy = open_lines(panel, opener) if find_path(panel, opener) else (None, None)
        if lines is None:
            continue
        paths[opener] = xy
        shot = os.path.join(SHOTS, 'bs4p4_%s_%s.bmp' % (panel, opener))
        script += ['echo @@ ' + opener] + lines + ['state', 'p4', 'logs'] + ['get %s %d' % (feat, i) for i in ids] + ['shotbb ' + shot]
    out = run(script)
    blocks = split_blocks(out)
    nopen = 0
    bad_all = []
    for opener, feat in sp['variants']:
        if opener not in paths:
            bad_all.append('%s: no click path to the opener found' % opener)
            continue
        B = parse_block(blocks.get(opener, []))
        if not B['p4'] or not B['state']:
            bad_all.append('%s: no panel state: %r' % (opener, blocks.get(opener, [])[:4]))
            continue
        st = B['state']
        bad = []
        if st['panel'] != panel or st['opener'] != opener:
            bad.append('opened %s by %s' % (st['panel'], st['opener']))
        hl = st['highlight']
        if hl != '%s/%d' % (hi_name(panel, opener), sp['weight']):
            bad.append('highlight %s' % hl)
        bad += check_state(panel, opener, feat, B, B['G'])
        shot = os.path.join(SHOTS, 'bs4p4_%s_%s.bmp' % (panel, opener))
        if sp.get('bars') and os.path.exists(shot) and B['G'][(feat, 7)][0] == 0:
            bad += check_bars(panel, opener, B, shot, paths[opener])
        bad += [l for l in B['logs']]
        if bad:
            bad_all.append('%s: %s' % (opener, '; '.join(bad)))
        else:
            nopen += 1
    check(not bad_all and nopen == len(sp['variants']),
          '%s: %d variants opened by real clicks (item group, then the 0x1a hotspot); feature, highlight, items, readouts, gauges and slider '
          'markers match the game' % (panel, nopen), ' | '.join(bad_all[:4]))

# ------------------------------------------------------------------------------------------------------------ 2b: the game state changes, the 1 Hz poll
HZ = 'advance 1200'
MARK = ['events', 'echo ## act']      # discard the events of the set-up commands


def first_variant(panel):
    return SPEC[panel]['variants'][0]


def block(script, tag='x', game=True):
    return parse_block(split_blocks(run(['echo @@ ' + tag] + script, game=game))[tag])


def p4_after(panel, opener, feat, extra, game=True, ids=None):
    lines, xy = open_lines(panel, opener)
    ids = ids or get_ids(panel, SPEC[panel])
    return block(lines + extra + ['p4'] + ['get %s %d' % (feat, i) for i in ids] + ['logs'], game=game)


for panel in ('panelcfan', 'panelrain', 'panelfilt', 'panelheat', 'panelmist'):
    sp = SPEC[panel]
    opener, feat = first_variant(panel)
    # switch the machine on (and give it a new schedule / temperatures / humidity): the poll sees the ON change, reloads the panel, the new
    # values are shown
    pre = ['setp %s 1 1' % feat]
    if sp.get('sched'):
        pre += ['setp %s 3 %d' % (feat, 7 * 3600 + 1800), 'setp %s 6 %d' % (feat, 5 * 3600 + 600)]
    if panel == 'panelheat':
        for pid, v in ((100, 9.0), (101, 21.5), (102, 3.0), (103, 30.0)):
            lo, hi = dbits(v)
            pre.append('setp %s %d 0 %d %d' % (feat, pid, lo, hi))
    if panel == 'panelmist':
        for pid, v in ((0x6e, 35.0), (0x6f, 80.0)):
            lo, hi = dbits(v)
            pre.append('setp %s %d 0 %d %d' % (feat, pid, lo, hi))
    lines, xy = open_lines(panel, opener)
    # open with the machine off, then switch it on behind the terminal's back
    B = block(['setp %s 1 0' % feat] + lines + pre + [HZ, 'p4'] + ['get %s %d' % (feat, i) for i in get_ids(panel, sp)] + ['logs'])
    bad = check_state(panel, opener, feat, B, B['G'])
    if B['G'][(feat, 1)][0] != 1:
        bad.append('the game did not switch the machine on')
    check(not bad and B['p4']['reload'] == '0',
          '%s: ON changed behind the terminal -> the 1 Hz poll reloads the panel and every readout shows the new game values' % panel, '; '.join(bad))
    if sp.get('water'):
        other = 0x60 if True else 0x61
        B0 = block(lines + ['get %s 95' % feat])
        cur = B0['G'][(feat, 0x5f)][0]
        new = 0x61 if cur == 0x60 else 0x60
        B = block(lines + ['setp %s 95 %d' % (feat, new), HZ, 'p4'] + ['get %s %d' % (feat, i) for i in get_ids(panel, sp)])
        bad = check_state(panel, opener, feat, B, B['G'])
        check(not bad and B['G'][(feat, 0x5f)][0] == new and int(B['p4']['ws'], 16) == new,
              '%s: FEATURE_WATER_SOURCE changed behind the terminal (0x%x -> 0x%x) -> reload, water-source items follow' % (panel, cur, new), '; '.join(bad))

# the active-state panels: ACTIVE toggled by the game -> reload (osm, uv, tide, wave) / time-limit slider (sfan)
for panel in ('panelsfan', 'panelosm', 'paneluv', 'paneltide', 'panelwave'):
    sp = SPEC[panel]
    opener, feat = first_variant(panel)
    lines, xy = open_lines(panel, opener)
    B0 = block(lines + ['get %s 2' % feat])
    cur = B0['G'][(feat, 2)][0]
    new = 0 if cur else 1
    B = block(lines + ['setp %s 2 %d' % (feat, new), HZ, 'p4'] + ['get %s %d' % (feat, i) for i in get_ids(panel, sp)])
    bad = check_state(panel, opener, feat, B, B['G'])
    check(not bad and B['G'][(feat, 2)][0] == new and int(B['p4']['active']) == new,
          '%s: FEATURE_ACTIVE changed behind the terminal (%d -> %d) -> the poll reloads the panel (items, indicator, loop sound)' % (panel, cur, new),
          '; '.join(bad))

# sfan: the time limit is polled and redrawn without a reload; air flow bar follows the fan
opener, feat = first_variant('panelsfan')
lines, xy = open_lines('panelsfan', opener)
B = block(lines + ['setp %s 5 %d' % (feat, 2 * 3600 + 25 * 60), HZ, 'p4', 'get %s 5' % feat])
check(B['p4']['time'] == '2:25' and B['p4']['reload'] == '0', 'panelsfan: FEATURE_TIME_LIMIT changed behind the terminal -> the poll shows 2:25 without a reload', repr(B['p4']['time']))
B = block(lines + ['setp %s 5 0' % feat, HZ, 'p4'])
check(int(B['p4']['mask'], 16) >> 31 & 1 == 1 and int(B['p4']['mask'], 16) >> 1 & 1 == 0 and B['p4']['time'] == '0:0',
      'panelsfan: time limit 0 -> the poll switches item 0x1f on and item 1 off', B['p4']['mask'])

# the gauges that the game only changes through its simulation: driven through the fake feature table of the driver, which plays the game
FAKE = {
    'panelsfan': [(7, 0, 0.0), (5, 3600, 0.0), (2, 0, 0.0), (0x50, 0, 3.0)],
    'panelvent': [(7, 0, 0.0), (0x50, 0, 3.0)],
    'panelsump': [(7, 0, 0.0), (0x51, 0, 3.2), (0x52, 0, 5.0), (0x32, 0, 0.0)],
    'panelosm': [(7, 0, 0.0), (2, 0, 0.0), (0x2b, 0, 40.0), (0x2c, 0, 90.0), (0x32, 0, 0.0)],
    'paneluv': [(7, 0, 0.0), (2, 1, 0.0), (0x2b, 0, 40.0), (0x2c, 0, 90.0), (0x32, 0, 0.0)],
    'paneltide': [(7, 0, 0.0), (2, 0, 0.0), (0x2b, 0, 25.0)],
}
for panel, params in FAKE.items():
    sp = SPEC[panel]
    opener, feat = first_variant(panel)
    lines, xy = open_lines(panel, opener)
    sets = ['set %s %d %d %r' % (feat, i, a, d) for i, a, d in params]
    ids = get_ids(panel, sp)
    out = run(sets + lines + ['p4', 'shotbb ' + os.path.join(SHOTS, 'bs4p4_%s_fake0.bmp' % panel)] + ['get %s %d' % (feat, i) for i in ids], game=False)
    B = parse_block(out)
    bad = check_state(panel, opener, feat, B, B['G'])
    bad += check_bars(panel, opener, B, os.path.join(SHOTS, 'bs4p4_%s_fake0.bmp' % panel), xy)
    # new gauge values (one out of range: shown clamped), then the poll
    changes = []
    for gi, (bar, pid, mn, mx) in enumerate(sp.get('bars', [])):
        changes.append(('set %s %d 0 %r' % (feat, pid, [mx * 0.75, mx * 1.5][gi % 2]), pid))
    if sp.get('alarms'):
        changes.append(('set %s 0x32 %d 0' % (feat, sp['alarms']), 0x32))
    if panel == 'panelsump':
        changes.append(('set %s 0x52 0 85.0' % feat, 0x52))
    shot = os.path.join(SHOTS, 'bs4p4_%s_fake1.bmp' % panel)
    out = run(sets + lines + [c for c, _ in changes] + ['advance 400', 'p4', HZ, 'p4', 'shotbb ' + shot] + ['get %s %d' % (feat, i) for i in ids], game=False)
    B2 = parse_block(out)
    early, late = B2['p4s'][0], B2['p4s'][1]
    # the values reach the screen only with the 1 Hz poll
    if early['g0'] != B['p4']['g0']:
        bad.append('gauge changed before the 1 Hz poll: %s -> %s' % (B['p4']['g0'], early['g0']))
    B2['p4'] = late
    bad += check_state(panel, opener, feat, B2, B2['G'])
    bad += check_bars(panel, opener, B2, shot, xy)
    check(not bad, '%s: gauge values / alarm bits / tier icon follow the game after the 1 Hz poll (and not before); out-of-range values are clamped' % panel,
          '; '.join(bad))


# the active indicator of the schedule / range panels: the machine starts to run (FEATURE_ACTIVE 1) behind the terminal's back; the poll updates the
# indicator item (0x19) and starts the loop sound without a reload (the game decides ACTIVE from its schedule and clock, which the driver does not
# run: the fake feature table plays the game here)
FAKE_ON = {
    'panelcfan': [(7, 0, 0.0), (1, 1, 0.0), (2, 0, 0.0), (3, 21600, 0.0), (6, 43200, 0.0)],
    'panelfilt': [(7, 0, 0.0), (1, 1, 0.0), (2, 0, 0.0), (3, 21600, 0.0), (6, 43200, 0.0)],
    'panelrain': [(7, 0, 0.0), (1, 1, 0.0), (2, 0, 0.0), (3, 21600, 0.0), (6, 43200, 0.0), (0x5f, 0x60, 0.0)],
    'panelheat': [(7, 0, 0.0), (1, 1, 0.0), (2, 0, 0.0), (100, 0, 10.0), (101, 0, 20.0), (102, 0, 5.0), (103, 0, 15.0)],
    'panelmist': [(7, 0, 0.0), (1, 1, 0.0), (2, 0, 0.0), (0x6e, 0, 30.0), (0x6f, 0, 70.0), (0x5f, 0x60, 0.0)],
}
for panel, params in FAKE_ON.items():
    sp = SPEC[panel]
    opener, feat = first_variant(panel)
    lines, xy = open_lines(panel, opener)
    sets = ['set %s %d %d %r' % (feat, i, a, d) for i, a, d in params]
    ids = get_ids(panel, sp)
    gets = ['get %s %d' % (feat, i) for i in ids]
    B0 = parse_block(run(sets + lines + ['p4'] + gets, game=False))
    B = parse_block(run(sets + lines + ['set %s 2 1 0.0' % feat, HZ, 'p4'] + gets, game=False))
    bad = check_state(panel, opener, feat, B0, B0['G']) + check_state(panel, opener, feat, B, B['G'])
    if has_item(panel, 25) and not ((int(B['p4']['mask'], 16) >> 25) & 1 and not (int(B0['p4']['mask'], 16) >> 25) & 1):
        bad.append('item 0x19: %s -> %s' % (B0['p4']['mask'], B['p4']['mask']))
    check(not bad and B0['p4']['active'] == '0' and B['p4']['active'] == '1' and B['p4']['reload'] == '0',
          '%s: FEATURE_ACTIVE changed -> the 1 Hz poll switches the indicator (item 0x19) without a reload' % panel, '; '.join(bad))

# the sump: the tier icon (57x57) of the silt level and the alarm icon are painted; the icon follows the value
panel = 'panelsump'
opener, feat = first_variant(panel)
lines, xy = open_lines(panel, opener)
params = [(7, 0, 0.0), (0x51, 0, 3.2), (0x52, 0, 5.0), (0x32, 0, 0.0)]
sets = ['set %s %d %d %r' % (feat, i, a, d) for i, a, d in params]
pg = oracle.page(names.index(panel) + 1)


def icon_diff(bmp, tier):
    w, h, rows = bs4_oracle.read_bmp(bmp)
    o = [x for x in pg.ornaments if x['name'] == tier][0]
    iw, ih, stride, raw = pg.image_pixels(o['image'])
    for r in range(min(ih, 57)):
        src = raw[(ih - 1 - r) * stride:(ih - 1 - r) * stride + iw]
        for c in range(min(iw, 57)):
            if rows[xy[1] + o['y'] + r][xy[0] + o['x'] + c] != src[c]:
                return 'tier icon %s pixel (%d,%d)' % (tier, c, r)
    return None


bmp0 = os.path.join(SHOTS, 'bs4p4_panelsump_icons0.bmp')
bmp1 = os.path.join(SHOTS, 'bs4p4_panelsump_icons1.bmp')
run(sets + lines + ['shotbb ' + bmp0, 'set %s 0x52 0 85.0' % feat, 'set %s 0x32 %d 0' % (feat, 0x1000000), HZ, 'shotbb ' + bmp1], game=False)
bad = []
for what, bmp, tier in (('silt 5 %', bmp0, '0to9'), ('silt 85 %', bmp1, '81plus')):
    d = icon_diff(bmp, tier)
    if d:
        bad.append('%s: %s' % (what, d))
check(not bad, 'panelsump: the silt tier icon (0to9 -> 81plus) is painted from the polled value (the alarm icon is not: no page of BIOSYS4.DAT has the `dangerlev` ornament)', '; '.join(bad))

# ------------------------------------------------------------------------------------------------------------ 3: controls
def events_of(lines):
    """the `set` events printed after the `## act` marker"""
    out = []
    for l in lines:
        if l == '## act':
            out = []
        elif l.startswith('set '):
            out.append(l)
    return out


for panel, sp in SPEC.items():
    if 'offbtn' not in sp:
        continue
    opener, feat = first_variant(panel)
    lines, xy = open_lines(panel, opener)
    ident = {'on': 1, 'active': 2}[sp['kind']]
    # states: start with the machine off, press the "on" button, then the "off" button
    if panel == 'panelsfan':
        pre = ['setp %s 5 3600' % feat]          # a time limit, so that the activate button (item 1) is there
    else:
        pre = []
    inv = 1 if (sp.get('inactive1') or True) else 0
    script = ['echo @@ x', 'setp %s %d 0' % (feat, ident)] + pre + lines + MARK + ['clickh %s' % sp['offbtn'], 'p4', 'events', 'get %s %d' % (feat, ident),
              'clickh %s' % sp['onbtn'], 'p4', 'events', 'get %s %d' % (feat, ident), 'logs']
    out = run(script)
    # the three blocks are separated by the `p4` lines: parse by order
    p4s = [parse_kv(l) for l in out if l.startswith('p4 ')]
    evs = []
    cur = []
    for l in out:
        if l.startswith('set '):
            cur.append(l)
        elif l.startswith('p4 '):
            evs.append(cur)
            cur = []
    gets = [int(m.group(1)) for m in (re.match(r'get \S+ (?:1|2) = ok int=(-?\d+)', l) for l in out) if m]
    bad = []
    if len(p4s) < 2:
        bad.append('no panel state')
    else:
        want_on = '%s 1' % ('on' if sp['kind'] == 'on' else 'active')
        k = 'on' if sp['kind'] == 'on' else 'active'
        if p4s[0][k] != '1' or p4s[1][k] != '0':
            bad.append('devOn/devActive after the two clicks: %s %s' % (p4s[0][k], p4s[1][k]))
        exp_on = ['set %s %d 1 0 0' % (feat, ident)]
        exp_off = ['set %s %d 0 0 0' % (feat, ident)]
        # the event of the first click is read before `p4` ... events are listed after `p4`: evs[1] holds the first click's set, evs[2] the second's
        got = events_of(out)
        if got != exp_on + exp_off:
            bad.append('events %r' % got)
        if gets[0] != 1 or gets[-1] != 0:
            bad.append('game state after the clicks %r' % gets)
    check(not bad, '%s: the %s / %s buttons (real clicks) send set parameter %d (%d, then %d) and the game applies it' %
          (panel, sp['offbtn'], sp['onbtn'], ident, 1, 0), '; '.join(bad))

# the water-source buttons
for panel in ('panelrain', 'panelmist'):
    opener, feat = first_variant(panel)
    lines, xy = open_lines(panel, opener)
    out = run(['echo @@ x'] + lines + MARK + ['clickh 18_condon', 'p4', 'get %s 95' % feat, 'clickh 17_osmon', 'p4', 'get %s 95' % feat, 'events', 'logs'])
    p4s = [parse_kv(l) for l in out if l.startswith('p4 ')]
    gets = [int(m.group(1)) for m in (re.match(r'get \S+ 95 = ok int=(-?\d+)', l) for l in out) if m]
    got = events_of(out)
    bad = []
    if gets != [0x61, 0x60]:
        bad.append('game water source %r' % gets)
    if got != ['set %s 95 97 0 0' % feat, 'set %s 95 96 0 0' % feat]:
        bad.append('events %r' % got)
    if len(p4s) == 2 and [int(p['ws'], 16) for p in p4s] != [0x61, 0x60]:
        bad.append('panel water source %r' % [p['ws'] for p in p4s])
    if len(p4s) == 2 and [(int(p['mask'], 16) >> 17) & 3 for p in p4s] != [1, 2]:
        bad.append('items 0x11 / 0x12 %r' % [p['mask'] for p in p4s])
    check(not bad, '%s: the water-source buttons send FEATURE_WATER_SOURCE 0x61 (condensate) and 0x60 (osmosis tank) and the game applies them' % panel,
          '; '.join(bad))


# ---- sliders
def drag(px, py, rc, tx_local, ty_local=None):
    """screen coordinates of a drag that grabs the marker `rc` (page rect) in its middle and releases it at the local x `tx_local`"""
    sx, sy = px + (rc[0] + rc[2]) // 2, py + (rc[1] + rc[3]) // 2
    tx = px + tx_local
    return ['down %d %d' % (sx, sy), 'move %d %d' % ((sx + tx) // 2, sy), 'move %d %d' % (tx, sy), 'up %d %d' % (tx, sy)], (rc[0] + rc[2]) // 2 - rc[0]


def sched_seconds(left):
    return left * 600 - 0x3840


for panel in ('panelcfan', 'panelrain', 'panelfilt'):
    sp = SPEC[panel]
    opener, feat = first_variant(panel)
    lines, xy = open_lines(panel, opener)
    px, py = xy
    base = ['setp %s 1 1' % feat, 'setp %s 3 %d' % (feat, 6 * 3600), 'setp %s 6 %d' % (feat, 12 * 3600)] + lines + ['p4']
    B = block(base)
    mon, moff = rect(B['p4']['markOn']), rect(B['p4']['markOff'])
    bad = []
    results = []
    for what, local in (('left end', 0), ('right end', 189), ('middle', 100)):
        dr, off = drag(px, py, mon, local)
        B = block(base + MARK + dr + ['p4', 'events', 'get %s 3' % feat, 'get %s 6' % feat])
        left = min(max(local - off, 0x18), 0xa8)
        on = sched_seconds(left)
        offs = 6 * 3600 + 12 * 3600
        dur = offs - on if offs >= on else offs + 86400 - on
        want = ['set %s 3 %d 0 0' % (feat, on), 'set %s 6 %d 0 0' % (feat, dur)]
        if B['events'] != want:
            bad.append('%s: events %r want %r' % (what, B['events'], want))
        if (B['G'][(feat, 3)][0], B['G'][(feat, 6)][0]) != (on, dur):
            bad.append('%s: game %r want %r' % (what, (B['G'][(feat, 3)][0], B['G'][(feat, 6)][0]), (on, dur)))
        if rect(B['p4']['markOn'])[0] != marker_x(on // 3600, (on % 3600) // 60):
            bad.append('%s: marker left %d, want %d' % (what, rect(B['p4']['markOn'])[0], marker_x(on // 3600, (on % 3600) // 60)))
        results.append((what, left, on))
    # the off marker
    dr, off = drag(px, py, moff, 60)
    B = block(base + MARK + dr + ['p4', 'events', 'get %s 3' % feat, 'get %s 6' % feat])
    left = min(max(60 - off, 0x18), 0xa8)
    offs = sched_seconds(left)
    on = 6 * 3600
    dur = offs - on if offs >= on else offs + 86400 - on
    want = ['set %s 3 %d 0 0' % (feat, on), 'set %s 6 %d 0 0' % (feat, dur)]
    if B['events'] != want or (B['G'][(feat, 3)][0], B['G'][(feat, 6)][0]) != (on, dur):
        bad.append('off marker: events %r game %r want %r' % (B['events'], (B['G'][(feat, 3)][0], B['G'][(feat, 6)][0]), (on, dur)))
    check(not bad, '%s: schedule slider (real drags): the on marker at both ends (24 px -> 0:00, 168 px -> 24:00) and in the middle, and the off '
          'marker, set TIME_ACTIVATE / DAILY_TIME_LIMIT and the game applies them' % panel, '; '.join(bad))

# sfan: the time-limit slider
panel = 'panelsfan'
opener, feat = first_variant(panel)
lines, xy = open_lines(panel, opener)
px, py = xy
base = ['setp %s 2 0' % feat, 'setp %s 5 %d' % (feat, 12 * 3600)] + lines + ['p4']
B = block(base)
mk = rect(B['p4']['marker'])
bad = []
for what, local in (('left end', 0), ('right end', 189), ('middle', 80)):
    dr, off = drag(px, py, mk, local)
    B = block(base + MARK + dr + ['p4', 'events', 'get %s 5' % feat])
    left = min(max(local - off, 0x18), 0xa8)
    secs = sched_seconds(left)
    if B['events'] != ['set %s 5 %d 0 0' % (feat, secs)] or B['G'][(feat, 5)][0] != secs:
        bad.append('%s: events %r game %r want %d' % (what, B['events'], B['G'].get((feat, 5)), secs))
    if rect(B['p4']['marker'])[0] != marker_x(secs // 3600, (secs % 3600) // 60):
        bad.append('%s: marker %r' % (what, B['p4']['marker']))
check(not bad, 'panelsfan: time-limit slider (real drags) at both ends (0 h, 24 h) and in the middle sets FEATURE_TIME_LIMIT and the game applies it', '; '.join(bad))


# value-range sliders
def range_case(panel, key, params, scale, lo_clamp, hi_val, ids_for_get, row):
    opener, feat = first_variant(panel)
    lines, xy = open_lines(panel, opener)
    px, py = xy
    sets = []
    for pid, v in params:
        l, h = dbits(v)
        sets.append('setp %s %d 0 %d %d' % (feat, pid, l, h))
    base = ['setp %s 1 1' % feat] + sets + lines + ['p4']
    B = block(base)
    f = B['p4'][key].split(',')
    mA, mB = tuple(int(x) for x in f[2:6]), tuple(int(x) for x in f[6:10])
    bad = []
    vb = float(f[1])
    pa, pb = ids_for_get
    # A to the far left
    for what, grab, local in (('first marker to the left end', mA, 0), ('second marker to the right end', mB, 189), ('first marker to the middle', mA, 50)):
        dr, off = drag(px, py, grab, local)
        B = block(base + MARK + dr + ['p4', 'events', 'get %s %d' % (feat, pa), 'get %s %d' % (feat, pb)])
        f2 = B['p4'][key].split(',')
        nA, nB = tuple(int(x) for x in f2[2:6]), tuple(int(x) for x in f2[6:10])
        dragged_first = grab is mA
        left = min(max(local - off, 0x18), 0xa8)
        if dragged_first:
            expA = (left - 0x18) * scale
            expB = (mB[0] - 0x18) * scale
            if nA[0] != left:
                bad.append('%s: marker left %d want %d' % (what, nA[0], left))
        else:
            expA = (mA[0] - 0x18) * scale
            expB = (left - 0x18) * scale
            if nB[0] != left:
                bad.append('%s: marker left %d want %d' % (what, nB[0], left))
        evs = B['events']
        wl, wh = dbits(expA)
        wl2, wh2 = dbits(expB)
        want = ['set %s %d 0 %d %d' % (feat, pa, wl, wh), 'set %s %d 0 %d %d' % (feat, pb, wl2, wh2)]
        if evs != want:
            bad.append('%s: events %r want %r' % (what, evs, want))
        ga, gb = B['G'][(feat, pa)][1], B['G'][(feat, pb)][1]
        if abs(ga - expA) > 1e-9 or abs(gb - expB) > 1e-9:
            bad.append('%s: game %r want %r' % (what, (ga, gb), (expA, expB)))
    # the first marker may not pass the second
    dr, off = drag(px, py, mA, 189)
    B = block(base + MARK + dr + ['p4'])
    f2 = B['p4'][key].split(',')
    nA, nB = tuple(int(x) for x in f2[2:6]), tuple(int(x) for x in f2[6:10])
    if nA[2] != nB[0] - 1:
        bad.append('first marker passed the second: %r %r' % (nA, nB))
    return bad


bad = range_case('panelheat', 'heatday', [(100, 12.0), (101, 25.0), (102, 5.0), (103, 30.0)], SCALE_HEAT, 0, 40.0, (100, 101), 0x51)
bad += range_case('panelheat', 'heatnight', [(100, 12.0), (101, 25.0), (102, 5.0), (103, 30.0)], SCALE_HEAT, 0, 40.0, (102, 103), 0xa8)
check(not bad, 'panelheat: both value-range sliders (day, night; real drags) at both ends and in the middle set the temperatures (100..103) and the game applies them; the first marker does not pass the second', '; '.join(bad))
bad = range_case('panelmist', 'mist', [(0x6e, 30.0), (0x6f, 70.0)], SCALE_MIST, 0, 100.0, (0x6e, 0x6f), 0x51)
check(not bad, 'panelmist: the humidity range slider (real drags) sets FEATURE_ACTIVATE / DEACTIVATE_HUMIDITY and the game applies them', '; '.join(bad))

# ------------------------------------------------------------------------------------------------------------ 4: close, reopen, same state
bad = []
nre = 0
for panel, sp in SPEC.items():
    for opener, feat in sp['variants'][:2]:
        lines, xy = open_lines(panel, opener)
        out = run(['echo @@ a'] + lines + ['p4', 'act 0x1b', 'state', 'p4'] + ['clickh %s' % opener] + ['p4'])
        p4s = [parse_kv(l) for l in out if l.startswith('p4 ')]
        states = [l for l in out if l.startswith('state ')]
        if len(p4s) < 2 or not states or ' panel=- ' not in states[0]:
            bad.append('%s/%s: close failed %r' % (panel, opener, states[:1]))
            continue
        # after the close the `p4` line still shows the old page; the reopened one is the last
        a, b = p4s[0], p4s[-1]
        for k in ('feature', 'on', 'active', 'loop', 'reloadname', 'mask', 'sched', 'time', 'g0', 'g1', 'tier', 'ws', 'heatday', 'heatnight', 'mist'):
            if a.get(k) != b.get(k):
                bad.append('%s/%s: %s %r -> %r' % (panel, opener, k, a.get(k), b.get(k)))
        nre += 1
check(not bad and nre == sum(len(sp['variants'][:2]) for sp in SPEC.values()),
      'every panel closes (verb 0x1b) and reopens with the same state (%d panel/variant pairs)' % nre, '; '.join(bad[:4]))


# ------------------------------------------------------------------------------------------------------------ 5: the real game, headless
# build/biosys --game --script: the terminal entered from a game page, three clicks to `air`, the cfan panel (variant 14_cfans1) opened by real
# clicks, switched on by its button: the panel shows the ON state and the loop sound of the running fan (loop 2) starts
def clicks_to(page):
    paths = {names.index('menuscreen') + 1: []}
    queue = [names.index('menuscreen') + 1]
    while queue:
        n = queue.pop(0)
        s = Sim(n)
        for i, h in enumerate(s.p.hotspots):
            if s.en[i] and h['action'][0] == 0x12:
                pt = s.point(i)
                t = h['action'][1]
                if t == names.index('email1') + 1:
                    t = names.index('emailpw') + 1
                if t == names.index('synth1') + 1:
                    t = names.index('synthpw') + 1
                if pt and t not in paths and 1 <= t <= 45:
                    paths[t] = paths[n] + [pt]
                    queue.append(t)
    cmds = []
    for x, y in paths[names.index(page) + 1]:
        cmds += ['click %d,%d' % (x, y), 'advance 50']
    return cmds


if os.path.exists(os.path.join(ROOT, 'build', 'biosys')):
    sv = os.path.join(ROOT, 'build', 'test', 'bs4p4', 'save')
    os.makedirs(sv, exist_ok=True)
    enter = ['advance 500', 'page NbrBIOS4', 'advance 500', 'clickhs BUTTON', 'advance 600', 'type RUSSELL\\n', 'advance 100']
    shot = os.path.join(SHOTS, 'bs4p4_game_panelcfan.bmp')
    script = (['bs4trace on'] + enter + clicks_to('air') + ['bs4hs techcfan', 'advance 50', 'bs4hs 14_cfans1', 'advance 50', 'dump bs4', 'bs4hs 2_activate',
              'advance 50', 'dump bs4', 'advance 1200', 'shot ' + shot, 'bs4hs 1_stop', 'advance 50', 'dump bs4'])
    r = subprocess.run([os.path.join(ROOT, 'build', 'biosys'), '--game', '--skip-intro', '--mute', '--seed', '1', '--log-events', '--save-dir', sv,
                        '--script', '; '.join(script)], capture_output=True, text=True, timeout=600)
    out = r.stdout.splitlines()
    dumps = [re.match(r"bs4 page (\S+) panel (\S+) opener '(\S*)' mask 0x([0-9a-f]+) pmask 0x([0-9a-f]+)", l) for l in out]
    dumps = [m for m in dumps if m]
    ev = [l[6:] for l in out if l.startswith('event bs4')]
    check(len(dumps) == 3 and [d.group(2) for d in dumps] == ['panelcfan'] * 3 and [d.group(3) for d in dumps] == ['14_cfans1'] * 3 and
          [int(d.group(5), 16) for d in dumps] == [0xc, 0xa, 0xc] and 'bs4loop 2 start' in ev,
          'the real game (build/biosys --script): panelcfan opened by clicks from the terminal entry, turned on (item 1) and off (item 2) by its buttons, '
          'the loop sound of the running fan starts', repr((out[-12:], ev[-6:])))

print('%d passed, %d failed' % (npass, fails))
print('ALL OK' if not fails else '%d FAILED' % fails)
sys.exit(1 if fails else 0)
