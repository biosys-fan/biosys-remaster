#!/usr/bin/env python3
"""BS4 main-page dynamics (packet P3: src/engine/bs4main_*.cpp) against the independent oracle tests/bs4main_oracle.py.

1. table-driven, scripted host (feature parameters from the `set` table): the health table, the overlay / device-highlight table, the alarm
   tables, the probe-page table, the pod page, the menu screen and the mail marks: which ornaments are shown, which values and bar heights
   are drawn, pixel for pixel against the oracle's composition of the page
2. the real game logic (--game, NewGame.sav): the same pages fed by the world
3. screenshots build/shots/bs4main_*.bmp"""
import os
import random
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bs4_oracle  # noqa: E402
import bs4main_oracle as mo  # noqa: E402

ROOT = os.path.dirname(HERE)
BIN = os.path.join(ROOT, 'build', 'biosys-bs4test')
OUT = os.path.join(ROOT, 'build', 'test', 'bs4main')
SHOTS = os.path.join(ROOT, 'build', 'shots')
os.makedirs(OUT, exist_ok=True)
os.makedirs(SHOTS, exist_ok=True)

fails = 0
counts = {}


def check(ok, what, detail=''):
    global fails
    if ok:
        print('PASS ' + what)
    else:
        fails += 1
        print('FAIL ' + what + (': ' + detail if detail else ''))


def run(script, *args):
    if isinstance(script, list):
        script = '\n'.join(script) + '\n'
    p = subprocess.run([BIN] + list(args), input=script, capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


oracle = bs4_oracle.Oracle()
T = mo.Tables()
C = mo.Composer(oracle)
NAMES = oracle.names
PAGE_NO = {n: i + 1 for i, n in enumerate(NAMES)}


def fset(feature, pid, i=0, d=0.0):
    return 'set %s %d %d %r' % (feature or '-', pid, i, float(d))


def segments(out):
    """split the output at `echo @@ tag` lines: tag -> list of lines"""
    seg = {}
    cur = None
    for line in out.splitlines():
        if line.startswith('@@ '):
            cur = line[3:]
            seg[cur] = []
        elif cur is not None:
            seg[cur].append(line)
    return seg


def parse_orn(lines):
    """-> {index: (name, flags)}"""
    r = {}
    for l in lines:
        m = re.match(r'orn (\d+) (\S+) (\d+)$', l)
        if m:
            r[int(m.group(1))] = (m.group(2), int(m.group(3)))
    return r


def parse_p3(lines):
    for l in lines:
        if l.startswith('p3 '):
            d = dict(re.findall(r'(\w+)=(\S+)', l.split(' text=')[0]))
            d['text'] = re.search(r'text=\[(.*?)\] mail=', l).group(1)
            m = re.search(r'mail=(\d+) img=(-?\d+) pod=(-?\d+) counter=(\d+) stamp=\[(.*)\]$', l)
            d['mail'], d['img'], d['pod'], d['counter'], d['stamp'] = int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4)), m.group(5)
            for k in ('dev', 'alarm', 'sensor', 'row'):
                d[k] = int(d[k])
            d['last'] = float(d['last'])
            d['ph'] = tuple(int(x) for x in d['ph'].split(','))
            return d
    return None


def parse_states(lines, key):
    for l in lines:
        if l.startswith(key + ' ') or l == key:
            return {int(a): int(b) for a, b in re.findall(r'(\d+)=(-?\d+)', l)}
    return None


def logs_of(lines, only=None):
    """the `log` lines of a run (the gauges of the main pages and the page the tool starts on log their own failures: `only` keeps a substring)"""
    r = [l[4:] for l in lines if l.startswith('log ')]
    return [l for l in r if only in l] if only else r


def gauge_rects(n):
    """the three power gauges are drawn by the page code with the real values: not part of the oracle's picture"""
    out = []
    for name in ('reservebar', 'powconbar', 'powgenbar'):
        o = C.find(n, name)
        if o:
            iw, ih, _, _ = oracle.page(n).image_pixels(o['image'])
            out.append((o['x'], o['y'], o['x'] + iw, o['y'] + ih))
    return out


def game_alarm_paint(surf, n, flags, mask=None):
    """the alarm icons of the real world (FEATURE_ALARMS of the page's probes) on top of an expected picture; returns the number of alarms"""
    page = NAMES[n - 1]
    rows = [r for r in T.alarm_pages if r['page'] == page]
    if not rows:
        return 0
    row = rows[0]
    ks = list(range(row['first'], row['last'] + 1))
    g = game_gets([(T.alarm_orns[k]['feature'], 0x32) for k in ks])
    st = {k: 1 if (g[(T.alarm_orns[k]['feature'], 0x32)] or (0, 0))[0] & (row['m1'] | row['m2']) else 0 for k in ks}
    if any(st.values()):
        C.paint_named(surf, n, 'warn')
    if (page_mask(n, flags) if mask is None else mask) & 0x10000:
        for k in ks:
            if st[k]:
                C.paint_named(surf, n, T.alarm_orns[k]['ornament'])
    return sum(st.values())


def existing_items(n):
    m = 0
    p = oracle.page(n)
    for item in range(32):
        pre = '%d_' % item
        if any(h['name'].startswith(pre) for h in p.hotspots) or any(o['type'] == 1 and o['name'].startswith(pre) for o in p.ornaments):
            m |= 1 << item
    return m


def first_orn(n, name):
    for i, o in enumerate(oracle.page(n).ornaments):
        if o['type'] == 1 and o['name'].lower() == name.lower():
            return i
    return None


def flags_list(n, orn):
    """the full flags list of page n from `orn` output (type-1 ornaments) over the record's flags"""
    fl = [o['flags'] for o in oracle.page(n).ornaments]
    for i, (_, f) in orn.items():
        fl[i] = f
    return fl


def page_mask(n, flags):
    return oracle.mask(n, flags)


def tmpname(*parts):
    return os.path.join(OUT, '_'.join(str(p) for p in parts))


# the section patching: a saved section with another visible mask (exe:00407000: mask at +0x474, probe sensor / row at +0x478 / +0x47c)
def patched_section(page, mask, sensor=None):
    src = tmpname('sec', page)
    if not os.path.exists(src):
        run(['load %s' % page, 'save %s' % src])
    b = bytearray(open(src, 'rb').read())
    assert len(b) == 0x480
    struct.pack_into('<I', b, 0x474, mask)
    if sensor is not None:
        struct.pack_into('<I', b, 0x478, sensor & 0xffffffff)
    dst = tmpname('sec', page, '%x' % mask, sensor)
    open(dst, 'wb').write(b)
    return dst


# ====================================================================================================================== 1. health
def health_pass():
    bad = []
    for pat in range(3):
        sets_a, sets_b = [], []
        stA, stB = {}, {}
        for i, r in enumerate(T.health):
            stA[i] = 2001 + (i + pat) % 3
            stB[i] = 2001 + (i + pat + 1) % 3
            sets_a.append(fset(r['feature'], 2000, stA[i]))
            sets_b.append(fset(r['feature'], 2000, stB[i]))
        rc, out, err = run(sets_a + ['load health', 'echo @@ a', 'orn', 'logs'] + sets_b + ['advance 1000', 'echo @@ b', 'orn', 'logs'])
        seg = segments(out)
        for tag, st in (('a', stA), ('b', stB)):
            orn = parse_orn(seg[tag])
            flags = flags_list(3, orn)
            for i, r in enumerate(T.health):
                want = mo.health_flags(st[i])
                for name, w in zip((r['dead'], r['unhealthy'], r['healthy']), want):
                    oi = first_orn(3, name)
                    if oi is None:
                        continue         # (the `ocean` row names ornaments the page does not have: exe:00488630 row 10)
                    if (flags[oi] & 1) != w:
                        bad.append('pat %d %s: %s hidden=%d want %d' % (pat, tag, name, flags[oi] & 1, w))
            if logs_of(seg[tag], 'SIM_HEALTH'):
                bad.append('unexpected log %r' % logs_of(seg[tag], 'SIM_HEALTH')[:2])
    missing = [n for r in T.health for n in (r['dead'], r['unhealthy'], r['healthy']) if first_orn(3, n) is None]
    check(not bad, 'health page: all 28 rows x 3 states (and the next states one tick later) show the dead / unhealthy / healthy ornament', '; '.join(bad[:4]))
    check(missing == ['oceand', 'oceanu', 'oceanh'], 'health table: the ornaments of the row `ocean` do not exist on the page (original bug, nothing to show)', repr(missing))

    # an unknown value is logged and the row left alone; a failed read at load is logged and stops the loop; a failed read in the tick is silent
    sets = [fset(r['feature'], 2000, 2003) for r in T.health[:5]] + [fset(T.health[5]['feature'], 2000, 2005)] + \
           [fset(r['feature'], 2000, 2001) for r in T.health[6:9]]
    rc, out, err = run(sets + ['load health', 'echo @@ a', 'orn', 'logs', 'advance 1000', 'echo @@ b', 'orn', 'logs'])
    seg = segments(out)
    flags = flags_list(3, parse_orn(seg['a']))
    ini = [o['flags'] for o in oracle.page(3).ornaments]
    rows_set = [first_orn(3, T.health[i]['healthy']) for i in range(5)]
    ok = all((flags[o] & 1) == 0 for o in rows_set)
    r5 = [first_orn(3, T.health[5][k]) for k in ('dead', 'unhealthy', 'healthy')]
    ok = ok and all(flags[o] == ini[o] for o in r5)                                   # the unknown value changed nothing
    r6 = first_orn(3, T.health[6]['dead'])
    ok = ok and (flags[r6] & 1) == 0 and (flags[first_orn(3, T.health[6]['healthy'])] & 1) == 1                    # later rows still processed
    r9 = [first_orn(3, T.health[9][k]) for k in ('dead', 'unhealthy', 'healthy')]
    ok = ok and all(flags[o] == ini[o] for o in r9)                                   # row 9 (o1) was never set: its read failed
    la = logs_of(seg['a'])
    ok = ok and any('Unknown value (2005) returned for parameter SIM_HEALTH_STATE for BiosFear feature \'s6\'' in l for l in la) and \
        any('Failed to get the value of parameter SIM_HEALTH_STATE for BiosFear feature \'o1\'' in l for l in la)
    check(ok, 'health page: an unknown state is logged and skipped, a failed read stops the load (logged) and is silent in the tick', repr(la))
    lb = logs_of(seg['b'], 'SIM_HEALTH')
    check(not any('Failed to get' in l for l in lb), 'health page: the tick logs nothing for a failed read', repr(lb))


# ====================================================================================================================== 2. overlays
def overlay_case(row_i, rnd):
    """returns a list of failure strings"""
    row = T.devices[row_i]
    page, n = row['page'], PAGE_NO[row['page']]
    feats = []
    for k in range(row['first'], row['last'] + 1):
        f = T.overlays[k]['feature']
        if f not in feats:
            feats.append(f)

    def values():
        v = {}
        for f in feats:
            if row['param'] == 0x50:
                v[(f, 0x50)] = (0, rnd.choice([0.0, 0.5, -1.0, 3.0]))
            else:
                v[(f, row['param'])] = (rnd.choice([0, 1, 1]), 0.0)
            if f in mo.LUNGS:
                v[(f, 0x11)] = (rnd.choice([0, 0, 0, 1]), 0.0)
            if f in mo.LUNGS or f in mo.PUMPS:
                v[(f, 0x14)] = (rnd.choice([0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b]), 0.0)
        return v
    va, vb = values(), values()
    sec = patched_section(page, 1 << row['bit'])
    reachable = bool(existing_items(n) & (1 << row['bit']))

    def sets(v):
        return [fset(f, pid, i, d) for (f, pid), (i, d) in v.items()]

    def expect(v):
        get = lambda f, pid: v.get((f, pid))
        return {k: mo.overlay_state(T.overlays[k]['feature'], T.overlays[k]['ornament'], row['param'], get)
                for k in range(row['first'], row['last'] + 1)}
    shot_a, shot_b = tmpname('ov', row_i, 'a.bmp'), tmpname('ov', row_i, 'b.bmp')
    rc, out, err = run(sets(va) + ['load %s' % page, 'restore %s' % sec, 'echo @@ a', 'overlays', 'p3', 'orn', 'shotpage %s' % shot_a] +
                       sets(vb) + ['advance 1000', 'echo @@ b', 'overlays', 'p3', 'orn', 'shotpage %s' % shot_b])
    seg = segments(out)
    bad = []
    for tag, v, shot in (('a', va, shot_a), ('b', vb, shot_b)):
        st = parse_states(seg[tag], 'overlays')
        p3 = parse_p3(seg[tag])
        want = expect(v)
        got = {k: st.get(k, 0) for k in want}
        if not reachable:        # the page has no item of that number: the mask bit can never be set, the row is never selected
            if p3['dev'] != -1 or st:
                bad.append('row %d %s: unreachable but deviceRow %d' % (row_i, tag, p3['dev']))
            continue
        if p3['dev'] != row_i:
            bad.append('row %d %s: deviceRow %d' % (row_i, tag, p3['dev']))
        if got != want:
            bad.append('row %d (%s %d..%d) %s: states %r want %r' % (row_i, page, row['first'], row['last'], tag, got, want))
        # the picture: the ornaments of the page's flags, then every lit overlay painted by name
        orn = parse_orn(seg[tag])
        flags = flags_list(n, orn)
        surf = C.base(n, flags)
        for k in sorted(want):
            if want[k]:
                C.paint_named(surf, n, T.overlays[k]['ornament'])
        d, first = mo.diff(surf, mo.read_page_bmp(shot))
        if d:
            bad.append('row %d %s: picture differs in %d pixels, first %r' % (row_i, tag, d, first))
    return bad, (sum(1 for k, s in expect(va).items() if s) + sum(1 for k, s in expect(vb).items() if s)) if reachable else 0, reachable


def overlays_pass():
    rnd = random.Random(7)
    bad = []
    lit = 0
    reach = 0
    for i in range(45):
        b, l, r = overlay_case(i, rnd)
        bad += b
        lit += l
        reach += r
    check(not bad and lit > 60 and reach >= 30, 'overlay table: the %d rows of 45 whose item bit exists on the page give the states and the picture of the oracle at load and one tick later (%d lit overlays); the others are never selected' % (reach, lit),
          '; '.join(bad[:4]))

    # the failing read: the build logs and stops, the row stays selected; the update skips it silently
    row = T.devices[0]
    sets = [fset(T.overlays[0]['feature'], 0xa, 1), fset(T.overlays[1]['feature'], 0xa, 1)]       # overlays 2 and 3 are unset
    sec = patched_section(row['page'], 1 << row['bit'])
    rc, out, err = run(sets + ['restore %s' % sec, 'echo @@ a', 'overlays', 'p3', 'logs', 'advance 1000', 'echo @@ b', 'logs'])
    seg = segments(out)
    p3 = parse_p3(seg['a'])
    la = logs_of(seg['a'])
    exp_log = "BS4: Failed to get the value of parameter 10 for BiosFear feature '%s'." % T.overlays[2]['feature']
    check(p3['dev'] == 0 and parse_states(seg['a'], 'overlays') == {0: 1, 1: 1} and exp_log in la and not any('feature' in l for l in logs_of(seg['b'])[len(la):]),
          'overlay build: a failed read is logged with the feature, ends the loop and keeps the row; the tick update skips it silently', repr(la[-3:]))


# ====================================================================================================================== 3. alarms
def probe_ignore(n, page, sensor):
    """the rectangles a probe page's picture may differ in (the value text, the pH-style bar, the marks painted by name)"""
    rects = []
    pr = [p for p in T.probes if p['page'] == page]
    if not pr:
        return rects
    pr = pr[0]
    l, t, r, b = pr['rect']
    rects.append((l, t, r, b))
    if pr['bar']:
        o = C.find(n, pr['bar'])
        if o:
            rects.append((o['x'], o['y'], o['x'] + 3, o['y'] + pr['height']))
    names = ['rangea', 'rangeb', 'rangec', 'xwind1', 'xwind2', 'xwind3', 'xsun1', 'xsun2', 'xsun3', 'xtemp1', 'xtemp2', 'xtemp3']
    if sensor is not None and 0 <= sensor < 22:
        names.append(T.sensors[sensor]['mark'])
    for nm in names:
        o = C.find(n, nm)
        if o:
            iw, ih, _, _ = oracle.page(n).image_pixels(o['image'])
            rects.append((o['x'], o['y'], o['x'] + iw, o['y'] + ih))
    return rects


def alarms_pass():
    rnd = random.Random(11)
    bad = []
    n_alarm = 0
    for ri, row in enumerate(T.alarm_pages):
        page, n = row['page'], PAGE_NO[row['page']]
        ks = list(range(row['first'], row['last'] + 1))

        def vals():
            v = {}
            for k in ks:
                f = T.alarm_orns[k]['feature']
                v[f] = rnd.choice([0, row['m1'], row['m2'], row['m1'] | row['m2'], 0x1fe00000 & ~(row['m1'] | row['m2']), row['m1'] | 0x80000000])
            return v
        va, vb = vals(), vals()

        def want(v):
            return {k: 1 if (v[T.alarm_orns[k]['feature']] & row['m1'] or v[T.alarm_orns[k]['feature']] & row['m2']) else 0 for k in ks}
        sa, sb = tmpname('al', ri, 'a.bmp'), tmpname('al', ri, 'b.bmp')
        rc, out, err = run([fset(f, 0x32, b) for f, b in va.items()] + ['load %s' % page, 'echo @@ a', 'alarms', 'p3', 'orn', 'shotpage %s' % sa] +
                           [fset(f, 0x32, b) for f, b in vb.items()] + ['advance 1000', 'echo @@ b', 'alarms', 'p3', 'orn', 'shotpage %s' % sb])
        seg = segments(out)
        for tag, v, shot in (('a', va, sa), ('b', vb, sb)):
            st = parse_states(seg[tag], 'alarms')
            w = want(v)
            n_alarm += sum(w.values())
            if {k: st.get(k, 0) for k in ks} != w or parse_p3(seg[tag])['alarm'] != ri:
                bad.append('%s %s states %r want %r' % (page, tag, {k: st.get(k, 0) for k in ks}, w))
            orn = parse_orn(seg[tag])
            flags = flags_list(n, orn)
            surf = C.base(n, flags)
            if any(w.values()):
                C.paint_named(surf, n, 'warn')
            if page_mask(n, flags) & 0x10000:
                for k in ks:
                    if w[k]:
                        C.paint_named(surf, n, T.alarm_orns[k]['ornament'])
            p3 = parse_p3(seg[tag])
            d, first = mo.diff(surf, mo.read_page_bmp(shot), probe_ignore(n, page, p3['sensor']))
            if d:
                bad.append('%s %s: picture differs in %d pixels, first %r' % (page, tag, d, first))
    check(not bad and n_alarm > 40, 'alarm tables: all 14 pages give the alarm states, the `warn` ornament and the red probe marks of the oracle at load and one tick later (%d alarms)' % n_alarm,
          '; '.join(bad[:4]))
    # a failed read at the build is logged with the sensor
    rc, out, err = run(['load co2', 'logs'])
    check(any("Failed to get the value of parameter FEATURE_ALARMS for BiosFear sensor 'fs1probe'." in l for l in logs_of(out.splitlines())),
          'alarm build: a failed FEATURE_ALARMS read is logged with the sensor name')


# ====================================================================================================================== 4. probe pages
def probe_values(pr):
    mn, mx = pr['min'], pr['max']
    vals = [mn - 3.0, mn, mn + (mx - mn) * 0.25, (mn + mx) / 2.0, mx - (mx - mn) * 0.01, mx, mx + 7.0, mn + (mx - mn) * 0.7777]
    if pr['page'] == 'co2':
        vals += [0.05, 0.1, 4.99, 5.0, 12.345]
    if pr['param'] == 0x23:
        vals += [9.999, 10.0]
    return vals


def probe_expect_surface(n, flags, pr, sensor, v, game=False):
    surf = C.base(n, flags)
    page = pr['page']
    if game:
        game_alarm_paint(surf, n, flags)
    if page == 'co2':
        C.paint_named(surf, n, 'rangea' if v < 0.1 else 'rangeb' if v < 5.0 else 'rangec')
    bar = mo.probe_bar(page, pr['param'], pr['min'], pr['max'], pr['height'], v)
    if pr['bar'] and bar > 0:
        o = C.find(n, pr['bar'])
        if o:
            rows = C.image_rows(n, o)
            for y in range(pr['height'] - bar, pr['height']):
                for x in range(3):
                    if y < len(rows) and x < len(rows[y]) and 0 <= o['y'] + y < 480:
                        surf[o['y'] + y][o['x'] + x] = rows[y][x]
    if sensor < 22:
        C.paint_named(surf, n, T.sensors[sensor]['mark'])
    marks = {'xwind': (7.0, 14.0), 'xsun': (33.0, 66.0), 'xtemp': (5.0, 18.0)}
    if page in marks:
        lo, hi = marks[page]
        C.paint_named(surf, n, '%s%d' % (page, 1 if v < lo else 2 if v < hi else 3))
    return surf


def probes_pass():
    rnd = random.Random(5)
    bad = []
    n_checked = 0
    rows_done = set()
    for pi, pr in enumerate(T.probes):
        page, n = pr['page'], PAGE_NO[pr['page']]
        forecast = page in ('xtemp', 'xwind', 'xsun')
        sensors = [22] if forecast else sorted(set([0, 9, 21, rnd.randrange(22), rnd.randrange(22)] + ([pr['default']] if pr['default'] >= 0 else [])))
        for s in sensors:
            vals = probe_values(pr)
            script = []
            shots = []
            for k, v in enumerate(vals):
                feat = '' if forecast else T.sensors[s]['feature']
                script += [fset(feat, pr['param'], 0, v), 'load %s' % page]
                if not forecast and (pr['default'] != s):
                    script += ['afteraction %s' % T.sensors[s]['hotspot']]
                shot = tmpname('pr', pi, s, k, '.bmp')
                shots.append(shot)
                script += ['echo @@ %d' % k, 'p3', 'orn', 'shotpage %s' % shot]
            rc, out, err = run(script)
            seg = segments(out)
            for k, v in enumerate(vals):
                p3 = parse_p3(seg[str(k)])
                orn = parse_orn(seg[str(k)])
                flags = flags_list(n, orn)
                ok = p3 is not None and p3['sensor'] == s and p3['row'] == pi and p3['last'] == v and p3['text'] == mo.probe_text(pr['param'], v)
                if not ok:
                    bad.append('%s sensor %d value %r: p3 %r' % (page, s, v, p3))
                    continue
                surf = probe_expect_surface(n, flags, pr, s, v)
                d, first = mo.diff(surf, mo.read_page_bmp(shots[k]), [pr['rect']])
                if d:
                    bad.append('%s sensor %d value %r: picture differs in %d pixels, first %r' % (page, s, v, d, first))
                n_checked += 1
                rows_done.add(pi)
    check(not bad and len(rows_done) == 20 and n_checked > 400,
          'probe pages: all 20 rows of the probe-page table show the value text, the bar height (pixels) and the marks of the oracle for %d (sensor, value) cases' % n_checked,
          '; '.join(bad[:3]))

    # the 1 Hz read repaints only when the text changes: a change below the displayed precision keeps the old value
    pr = T.probes[3]       # press, %.1f
    feat = T.sensors[4]['feature']
    rc, out, err = run([fset(feat, pr['param'], 0, 1000.04), 'load press', 'afteraction %s' % T.sensors[4]['hotspot'], fset(feat, pr['param'], 0, 1000.049),
                        'advance 1000', 'echo @@ a', 'p3', fset(feat, pr['param'], 0, 1000.06), 'advance 1000', 'echo @@ b', 'p3',
                        'echo @@ c', fset(feat, pr['param'], 0, 1000.04), 'advance 1000', 'p3'])
    seg = segments(out)
    pa, pb = parse_p3(seg['a']), parse_p3(seg['b'])
    check(pa['last'] == 1000.04 and pb['last'] == 1000.06,
          'probe tick: a value whose text does not change is not taken over (%.3f stays), a changed text is (%.3f)' % (pa['last'], pb['last']))
    # the tick reads a failing parameter as 0, the load logs it
    rc, out, err = run(['load press', 'afteraction 16_senss1', 'logs'])
    check(any("Failed to get the value of parameter 29 for BiosFear sensor 'fs1probe'." in l for l in logs_of(out.splitlines())),
          'probe read: a failed read at selection is logged with the parameter and the sensor')
    rc, out, err = run(['load xtemp', 'logs'])
    check(any('Failed to get the value of world parameter 1021.' in l for l in logs_of(out.splitlines())), 'probe read: a failed world parameter is logged as such')
    rc, out, err = run([fset('fs1probe', 0x1d, 0, 1013.0), 'load press', 'afteraction 16_senss1', 'echo @@ a', 'p3', 'advance 1000', 'echo @@ b', 'p3'])
    seg = segments(out)
    check(parse_p3(seg['a'])['last'] == 1013.0 and parse_p3(seg['b'])['last'] == 1013.0, 'probe tick: a parameter that stays readable keeps its value')

    # OnMainPageShown: a probe page without the probe item (bit 17) forgets the sensor when it is shown again, with it keeps it
    for keep in (0, 1):
        pr = T.probes[7]          # swc
        sec_mask = 0x10000 | (0x20000 if keep else 0)
        sec_path = tmpname('probe_sec', keep)
        rc, out, err = run([fset('fs3probe', 0x21, 0, 12.5), 'load swc', 'afteraction 16_senss3', 'save %s' % sec_path])
        b = bytearray(open(sec_path, 'rb').read())
        struct.pack_into('<I', b, 0x474, sec_mask)
        open(sec_path, 'wb').write(b)
        rc, out, err = run([fset('fs3probe', 0x21, 0, 12.5), 'restore %s' % sec_path, 'echo @@ a', 'p3'])
        p3 = parse_p3(segments(out)['a'])
        saved = struct.unpack_from('<I', b, 0x478)[0]
        check(saved == 2 and (p3['sensor'] == 2 if keep else p3['sensor'] == -1 and p3['row'] == -1),
              'OnMainPageShown after loading a game: probe sensor %s when the page mask has bit 17 %s' % ('kept' if keep else 'forgotten', 'set' if keep else 'clear'), repr(p3))


# ====================================================================================================================== 5. pod, menu, mail
def pod_pass():
    n = PAGE_NO['pod']
    bad = []
    for pos in range(0x15, 0x1a):
        rc, out, err = run([fset('fPod', 0x85, pos), 'load pod', 'echo @@ a', 'orn', 'hotspots', 'p3', fset('fPod', 0x85, 0x15 + (pos - 0x14) % 5), 'advance 1000',
                            'echo @@ b', 'orn', 'hotspots', 'p3'])
        seg = segments(out)
        for tag, want_pos in (('a', pos), ('b', 0x15 + (pos - 0x14) % 5)):
            orn = parse_orn(seg[tag])
            flags = flags_list(n, orn)
            hot = {}
            for l in seg[tag]:
                m = re.match(r'hot \d+ (\S+) -?\d+ -?\d+ -?\d+ -?\d+ flags=(\d+)', l)
                if m and m.group(1) not in hot:
                    hot[m.group(1)] = int(m.group(2))
            for p, name in mo.POD_NAMES.items():
                oi = first_orn(n, name)
                if oi is not None and (flags[oi] & 1) != (0 if p == want_pos else 1):
                    bad.append('pos %#x %s: ornament %s hidden=%d' % (want_pos, tag, name, flags[oi] & 1))
                if name in hot and (hot[name] & 1) != (0 if p == want_pos else 1):
                    bad.append('pos %#x %s: hotspot %s disabled=%d' % (want_pos, tag, name, hot[name] & 1))
            if parse_p3(seg[tag])['pod'] != want_pos:
                bad.append('pos %#x %s: remembered %r' % (want_pos, tag, parse_p3(seg[tag])['pod']))
    check(not bad, 'pod page: the ornament and the hotspot of the position (0x15..0x19 of fPod 0x85) are the only ones enabled at load and after a change', '; '.join(bad[:4]))
    rc, out, err = run([fset('fPod', 0x85, 0x30), 'load pod', 'logs'])
    check(any('Invalid pod position value 48' in l for l in logs_of(out.splitlines())), 'pod page: an invalid position is logged')
    rc, out, err = run(['load pod', 'logs'])
    check(any('Failed to get the value of parameter FEATURE_POD_POSITION' in l for l in logs_of(out.splitlines())), 'pod page: a failed read is logged')


def menu_pass():
    n = PAGE_NO['menuscreen']
    rnd = random.Random(3)
    bad = []
    seen = set()
    for case in range(12):
        h = [(rnd.choice([0, 1]), rnd.choice([0.0, 10.0, 19.999, 20.0, 35.0, 49.999, 50.0, 50.001, 80.0, 100.0])) for _ in range(4)]
        power = rnd.choice([0, 1, 5])
        ids = [0x837, 0x834, 0x835, 0x836]
        sets = [fset('', pid, i, d) for pid, (i, d) in zip(ids, h)] + [fset('', 0x424, power)]
        h2 = [(rnd.choice([0, 1]), rnd.choice([0.0, 30.0, 60.0, 90.0])) for _ in range(4)]
        sets2 = [fset('', pid, i, d) for pid, (i, d) in zip(ids, h2)] + [fset('', 0x424, 1 - min(power, 1))]
        rc, out, err = run(sets + ['load menuscreen', 'echo @@ a', 'orn'] + sets2 + ['advance 1000', 'echo @@ b', 'orn'])
        seg = segments(out)
        for tag, hh, pw in (('a', h, power), ('b', h2, 1 - min(power, 1))):
            flags = flags_list(n, parse_orn(seg[tag]))
            shown = mo.menu_names(hh) | {'nopower' if pw else 'okpower'}
            seen |= shown
            for name in mo.MENU_ALL:
                oi = first_orn(n, name)
                if oi is not None and (flags[oi] & 1) != (0 if name in shown else 1):
                    bad.append('case %d %s: %s hidden=%d' % (case, tag, name, flags[oi] & 1))
    check(not bad and len(seen) >= 14, 'menuscreen: the status ornaments of the four biomes (health / rejected / dying / dead), biogood / biobad and nopower / okpower follow the values at load and in the tick (%d of 20 shown at some time)' % len(seen), '; '.join(bad[:4]))
    rc, out, err = run(['load menuscreen', 'logs'])
    check(any('SIM_DEAD_BIOME_HEALTH' in l for l in logs_of(out.splitlines())), 'menuscreen: a failed read is logged (first parameter 0x837)')


def mail_pass():
    bad = []
    for page in ('emailin', 'email1', 'emailout', 'tech'):
        n = PAGE_NO[page]
        ew = C.find(n, 'ewait')
        ewo = C.find(n, 'ewaitoff')
        nm = C.find(n, 'numail')
        shots = [tmpname('mail', page, k, '.bmp') for k in range(4)]
        rc, out, err = run(['clock Day 3 08:15', fset('', 0x438, 0), 'session mail 0', 'load %s' % page, 'echo @@ 0', 'p3', 'orn', 'hotspots', 'shotpage %s' % shots[0],
                            'advance 1000', 'echo @@ 1', 'p3', 'shotpage %s' % shots[1],
                            fset('', 0x438, 1), 'advance 1000', 'echo @@ 2', 'p3', 'orn', 'hotspots', 'shotpage %s' % shots[2],
                            'advance 1000', 'echo @@ 3', 'p3', 'shotpage %s' % shots[3],
                            'advance 1000', 'echo @@ 4', 'p3', 'afteraction bolder', 'echo @@ 5', 'p3', 'orn', 'hotspots', 'advance 2000', 'echo @@ 6', 'p3'])
        seg = segments(out)
        P = {k: parse_p3(seg[k]) for k in seg}
        if P['0']['mail'] != 0 or P['1']['mail'] != 0:
            bad.append('%s: mail state before arrival %d/%d' % (page, P['0']['mail'], P['1']['mail']))
        if P['2']['mail'] != 1 or P['2']['stamp'] != 'Day 3 08:15':
            bad.append('%s: arrival: state %d stamp %r' % (page, P['2']['mail'], P['2']['stamp']))
        # the blink: even page counter ewait, odd ewaitoff; the picture of the mark and of the banner
        for k, c in (('2', P['2']), ('3', P['3'])):
            if ew is None:
                break
            want_img = ew['image'] if c['counter'] % 2 == 0 else ewo['image']
            if k == '3' and c['img'] != want_img:
                bad.append('%s: blink image %d at counter %d want %d' % (page, c['img'], c['counter'], want_img))
            n_ = {'2': 2, '3': 3}[k]
            surf = C.base(n, flags_list(n, parse_orn(seg['2'] if k == '2' else seg['2'])))
            if k == '3':
                surf = C.base(n, flags_list(n, parse_orn(seg['2'])))
            img = ew if want_img == ew['image'] else ewo
            if k == '2':
                img = ew if c['counter'] % 2 == 0 else ewo
            C.paint(surf, n, img, opaque=True, at=(ew['x'], ew['y']))
            if nm:
                C.paint(surf, n, nm, opaque=True)
            bm = mo.read_page_bmp(shots[n_])
            if page == 'emailin':
                pass
            d, first = mo.diff(surf, bm, [(253, 116, 400, 140)])
            if d:
                bad.append('%s counter %d: picture differs in %d pixels, first %r' % (page, c['counter'], d, first))
        if P['4']['mail'] != 1 or P['5']['mail'] != 2 or P['6']['mail'] != 2:
            if page == 'emailin' or P['5']['mail'] != 1:
                bad.append('%s: after bolder: %r' % (page, [P[k]['mail'] for k in '456']))
        if page == 'emailin':
            def state_of(tag):
                orn = parse_orn(seg[tag])
                fl = flags_list(n, orn)
                hot = {}
                for l in seg[tag]:
                    m = re.match(r'hot \d+ (\S+) -?\d+ -?\d+ -?\d+ -?\d+ flags=(\d+)', l)
                    if m and m.group(1) not in hot:
                        hot[m.group(1)] = int(m.group(2))
                return (fl[first_orn(n, 'bolder')] & 1, fl[first_orn(n, 'extra')] & 1, hot['bolder'] & 1, hot['extra'] & 1)
            for tag, want in (('0', (1, 1, 1, 1)), ('2', (0, 1, 0, 1)), ('5', (1, 0, 1, 0))):
                if state_of(tag) != want:
                    bad.append('emailin %s: bolder/extra ornament+hotspot hidden %r want %r' % (tag, state_of(tag), want))
        else:
            if P['5']['mail'] != 1:
                bad.append('%s: afteraction bolder on another page changed the mail state' % page)
    check(not bad, 'mail: arrival on the story flag (0x438), the arrival stamp (query 0xb), the blinking ewait / ewaitoff mark and the numail banner (pictures), the bolder / extra marks of emailin, "read" on `bolder`', '; '.join(bad[:4]))
    # the stamp text of the open mail (mask bit 0x20000) and of the `extra` hotspot
    n = PAGE_NO['emailin']
    a, b = tmpname('stamp_a.bmp'), tmpname('stamp_b.bmp')
    rc, out, err = run(['clock Day 3 08:15', 'session mail 2', 'session stamp Day 3 08:15', 'load emailin', 'shotpage %s' % a, 'afteraction extra', 'shotpage %s' % b])
    pa, pb = mo.read_page_bmp(a), mo.read_page_bmp(b)
    d, first = mo.diff(pa, pb)
    region = all(253 <= x < 450 and 110 <= y < 140 for x, y in [first[:2]]) if first else False
    check(d > 20 and region, 'emailin: `extra` draws the arrival stamp at (253,116) (%d pixels)' % d, repr(first))


def selftest_pass():
    # the state of the pages is not touched when page-specific code is off
    rc, out, err = run(['load health', 'orn'], '--static')
    flags = parse_orn(out.splitlines())
    ini = {i: o['flags'] for i, o in enumerate(oracle.page(3).ornaments) if o['type'] == 1}
    check(all(f == ini[i] for i, (_, f) in flags.items()), '--static: the page records are not touched by the dynamics')


# ====================================================================================================================== 6. real game logic
def game_get(feature, pid):
    """the world's answer through the test tool: (int, double) or None"""
    rc, out, err = run(['get %s %d' % (feature or '-', pid)], '--game')
    m = re.search(r'= (ok|no) int=(-?\d+) dbl=(\S+)', out)
    return (int(m.group(2)) & 0xffffffff, float(m.group(3))) if m and m.group(1) == 'ok' else None


def game_gets(queries):
    """many gets in one run: {(feature, id): (int, double) | None}"""
    script = ['get %s %d' % (f or '-', pid) for f, pid in queries]
    rc, out, err = run(script, '--game')
    res = {}
    for q, l in zip(queries, [l for l in out.splitlines() if l.startswith('get ')]):
        m = re.search(r'= (ok|no) int=(-?\d+) dbl=(\S+)', l)
        res[q] = (int(m.group(2)) & 0xffffffff, float(m.group(3))) if m.group(1) == 'ok' else None
    return res


def game_health():
    n = 3
    g = game_gets([(r['feature'], 2000) for r in T.health])
    rc, out, err = run(['load health', 'echo @@ a', 'orn', 'logs'], '--game')
    seg = segments(out)
    flags = flags_list(n, parse_orn(seg['a']))
    bad = []
    states = []
    for r in T.health:
        st = g[(r['feature'], 2000)][0]
        states.append(st)
        want = mo.health_flags(st)
        for name, w in zip((r['dead'], r['unhealthy'], r['healthy']), want):
            oi = first_orn(n, name)
            if oi is not None and (flags[oi] & 1) != w:
                bad.append('%s: %s hidden=%d want %d' % (r['feature'], name, flags[oi] & 1, w))
    check(not bad and len(set(states)) >= 1 and not logs_of(seg['a']),
          'health page with the real game logic: the 28 zones show the state SIM_HEALTH_STATE reports (%s)' % ', '.join('%d x %d' % (states.count(s), s) for s in sorted(set(states))), '; '.join(bad[:3] + logs_of(seg['a'])[:2]))


def game_probes():
    bad = []
    done = 0
    pages_ok = set()
    for pi, pr in enumerate(T.probes):
        page, n = pr['page'], PAGE_NO[pr['page']]
        forecast = page in ('xtemp', 'xwind', 'xsun')
        sensors = [22] if forecast else list(range(22))
        queries = [('' if forecast else T.sensors[s]['feature'], pr['param']) for s in sensors]
        g = game_gets(queries)
        script = []
        for s in sensors:
            script += ['load %s' % page]
            if not forecast and pr['default'] != s:
                script += ['afteraction %s' % T.sensors[s]['hotspot']]
            script += ['echo @@ %d' % s, 'p3', 'logs']
        rc, out, err = run(script, '--game')
        seg = segments(out)
        for s, q in zip(sensors, queries):
            if not forecast and pr['default'] != -1 and s != pr['default'] and False:
                continue
            p3 = parse_p3(seg[str(s)])
            v = g[q]
            if v is None:
                if p3['sensor'] == s and not any('Failed to get the value' in l for l in logs_of(seg[str(s)])):
                    bad.append('%s sensor %d: read fails but no log' % (page, s))
                continue
            want = v[1]
            ok = p3['sensor'] == s and p3['row'] == pi and abs(p3['last'] - want) < 1e-12 and p3['text'] == mo.probe_text(pr['param'], want)
            if not ok:
                bad.append('%s sensor %d: p3 %r want %r (text %r)' % (page, s, p3, want, mo.probe_text(pr['param'], want)))
            else:
                done += 1
                pages_ok.add(page)
    check(not bad and len(pages_ok) >= 15 and done >= 100,
          'probe pages with the real game logic: %d (page, sensor) pairs on %d pages show the value the sensor parameter returns (text)' % (done, len(pages_ok)), '; '.join(bad[:3]))


def game_probe_pictures():
    """picture + bar of every probe page for the selectable sensors of the real game"""
    bad = []
    done = 0
    for pi, pr in enumerate(T.probes):
        page, n = pr['page'], PAGE_NO[pr['page']]
        forecast = page in ('xtemp', 'xwind', 'xsun')
        sensors = [22] if forecast else list(range(22))
        queries = [('' if forecast else T.sensors[s]['feature'], pr['param']) for s in sensors]
        g = game_gets(queries)
        script = []
        for s in sensors:
            script += ['load %s' % page]
            if not forecast and pr['default'] != s:
                script += ['afteraction %s' % T.sensors[s]['hotspot']]
            script += ['echo @@ %d' % s, 'p3', 'orn', 'shotpage %s' % tmpname('gpr', pi, s, '.bmp')]
        rc, out, err = run(script, '--game')
        seg = segments(out)
        for s, q in zip(sensors, queries):
            if g[q] is None:
                continue
            flags = flags_list(n, parse_orn(seg[str(s)]))
            surf = probe_expect_surface(n, flags, pr, s, g[q][1], True)
            d, first = mo.diff(surf, mo.read_page_bmp(tmpname('gpr', pi, s, '.bmp')), [pr['rect']] + gauge_rects(n))
            if d:
                bad.append('%s sensor %d: %d pixels, first %r' % (page, s, d, first))
            done += 1
    check(not bad and done >= 250, 'probe pages with the real game logic: bar height and marks of all %d (page, sensor) pairs match the oracle picture' % done, '; '.join(bad[:3]))


def game_overlays():
    """the device-highlight overlays follow the real machine states: switch machines through their parameters and look again"""
    bad = []
    flips = 0
    changed_rows = 0
    for ri, row in enumerate(T.devices):
        page, n = row['page'], PAGE_NO[row['page']]
        feats = []
        for k in range(row['first'], row['last'] + 1):
            f = T.overlays[k]['feature']
            if f not in feats:
                feats.append(f)
        sec = patched_section(page, 1 << row['bit'])
        # which parameter turns the machine on: ACTIVE (2) for the machines of the rows with parameter 2, OPEN (10) for the hatches / seals;
        # the vents of the AIR_FLOW rows are switched on through ACTIVE as well
        pid_set = 10 if row['param'] == 10 else 2 if row['param'] != 0x50 else 2
        qs = []
        for f in feats:
            qs += [(f, row['param']), (f, 0x11), (f, 0x14)]

        def expect(gets):
            gt = lambda f, pid: gets.get((f, pid))
            return {k: mo.overlay_state(T.overlays[k]['feature'], T.overlays[k]['ornament'], row['param'], gt) for k in range(row['first'], row['last'] + 1)}
        g0 = game_gets(qs)
        script = ['load %s' % page, 'restore %s' % sec, 'echo @@ a', 'overlays', 'p3', 'orn', 'shotpage %s' % tmpname('go', ri, 'a.bmp')]
        for f in feats:
            script += ['setp %s %d 1' % (f, pid_set)]
            if row['param'] != pid_set and row['param'] != 0x50:
                script += ['setp %s %d 1' % (f, row['param'])]
        script += ['advance 1000', 'echo @@ b', 'overlays', 'p3', 'orn', 'shotpage %s' % tmpname('go', ri, 'b.bmp')]
        for f in feats:
            script += ['get %s %d' % (f, row['param']), 'get %s %d' % (f, 0x11), 'get %s %d' % (f, 0x14)]
        script += ['echo @@ c']
        rc, out, err = run(script, '--game')
        seg = segments(out)
        if 'b' not in seg:
            bad.append('row %d %s: run failed rc=%d %s' % (ri, page, rc, err.strip()[-150:]))
            continue
        # the gets after the flip, in the same run (after segment b)
        raw = [l for l in out.splitlines() if l.startswith('get ')]
        g1 = {}
        for q, l in zip(qs, raw):
            m = re.search(r'= (ok|no) int=(-?\d+) dbl=(\S+)', l)
            g1[q] = (int(m.group(2)) & 0xffffffff, float(m.group(3))) if m.group(1) == 'ok' else None
        if not existing_items(n) & (1 << row['bit']):          # no such item on the page: the row is never selected
            if parse_p3(seg['a'])['dev'] != -1 or parse_states(seg['a'], 'overlays') or parse_states(seg['b'], 'overlays'):
                bad.append('row %d %s: unreachable row selected' % (ri, page))
            continue
        for tag, gets in (('a', g0), ('b', g1)):
            want = expect(gets)
            st = parse_states(seg[tag], 'overlays')
            got = {k: st.get(k, 0) for k in want}
            if got != want:
                bad.append('row %d %s %s: %r want %r' % (ri, page, tag, got, want))
            flags = flags_list(n, parse_orn(seg[tag]))
            surf = C.base(n, flags)
            for k in sorted(want):
                if want[k]:
                    C.paint_named(surf, n, T.overlays[k]['ornament'])
            game_alarm_paint(surf, n, flags, (1 << row['bit']) & existing_items(n))
            d, first = mo.diff(surf, mo.read_page_bmp(tmpname('go', ri, tag + '.bmp')), gauge_rects(n) + probe_ignore(n, page, None))
            if d:
                bad.append('row %d %s %s: picture %d pixels, first %r' % (ri, page, tag, d, first))
        if expect(g0) != expect(g1):
            changed_rows += 1
        flips += 1
    check(not bad and changed_rows >= 5, 'overlays with the real game logic: all 45 rows follow the machines (states and pictures), %d rows changed after the machines were switched' % changed_rows, '; '.join(bad[:3]))


def game_pod_menu_mail():
    n = PAGE_NO['pod']
    g = game_gets([('fPod', 0x85)])
    pos = g[('fPod', 0x85)][0]
    rc, out, err = run(['load pod', 'echo @@ a', 'orn', 'p3'], '--game')
    seg = segments(out)
    flags = flags_list(n, parse_orn(seg['a']))
    ok = all(((flags[first_orn(n, name)] & 1) == (0 if p == pos else 1)) for p, name in mo.POD_NAMES.items() if first_orn(n, name) is not None)
    check(ok and parse_p3(seg['a'])['pod'] == pos, 'pod page with the real game logic: shows the position the pod is in (%#x)' % pos)
    rc, out, err = run(['setp fPod 0x8f 1', 'get fPod 133', 'load pod', 'orn', 'p3'], '--game')

    # menuscreen
    n = PAGE_NO['menuscreen']
    g = game_gets([('', 0x837), ('', 0x834), ('', 0x835), ('', 0x836), ('', 0x424)])
    h = [g[('', i)] for i in (0x837, 0x834, 0x835, 0x836)]
    pw = 1 if g[('', 0x424)][0] else 0
    rc, out, err = run(['load menuscreen', 'echo @@ a', 'orn', 'logs'], '--game')
    seg = segments(out)
    flags = flags_list(n, parse_orn(seg['a']))
    shown = mo.menu_names([(i, d) for i, d in h]) | {'nopower' if pw else 'okpower'}
    bad = [name for name in mo.MENU_ALL if first_orn(n, name) is not None and (flags[first_orn(n, name)] & 1) != (0 if name in shown else 1)]
    check(not bad and not logs_of(seg['a']), 'menuscreen with the real game logic: biome health %s, power disconnected %d -> %s' % (
        ' '.join('%.1f' % d for _, d in h), pw, ' '.join(sorted(shown))), repr(bad) + repr(logs_of(seg['a'])))

    # mail: the story flag of the real game
    n = PAGE_NO['emailin']
    rc, out, err = run(['clock Day 9 10:30', 'story 0', 'load emailin', 'advance 1000', 'echo @@ a', 'p3', 'story 1', 'advance 1000', 'echo @@ b', 'p3', 'orn',
                        'afteraction bolder', 'echo @@ c', 'p3'], '--game')
    seg = segments(out)
    pa, pb, pc = parse_p3(seg['a']), parse_p3(seg['b']), parse_p3(seg['c'])
    check(pa['mail'] == 0 and pb['mail'] == 1 and pb['stamp'] == 'Day 9 10:30' and pc['mail'] == 2,
          'mail with the real game logic: the story flag (milestone bit 29) makes the mail arrive with the clock text; `bolder` marks it read', repr((pa['mail'], pb['mail'], pb['stamp'], pc['mail'])))


# ====================================================================================================================== 7. screenshots
def screenshots():
    pages = [
        ('health', ['load health']),
        ('pod', ['load pod']),
        ('menuscreen', ['load menuscreen']),
        ('probe_ph', ['load ph']),
        ('probe_co2', ['load co2', 'afteraction 16_senss1']),
        ('probe_press', ['load press', 'afteraction 16_senss4']),
        ('probe_xtemp', ['load xtemp']),
        ('probe_swc', ['load swc', 'afteraction 16_senso1']),
        ('mail_emailin', ['clock Day 9 10:30', 'story 0', 'load emailin', 'story 1', 'advance 1000', 'afteraction bolder']),
        ('mail_email1', ['clock Day 9 10:30', 'story 1', 'load email1', 'advance 1000']),
        ('emailout', ['clock Day 9 10:30', 'story 1', 'load emailout', 'advance 2000']),
        ('air_overlays', ['load air']),
        ('alarm_press', ['load press', 'afteraction 16_senss2']),
    ]
    made = 0
    for name, cmds in pages:
        script = list(cmds)
        if name == 'air_overlays':
            sec = patched_section('air', 1 << 5)
            feats = [T.overlays[k]['feature'] for k in range(0, 4)]
            script = ['load air', 'restore %s' % sec] + ['setp %s 10 1' % f for f in feats] + ['advance 1000']
        script += ['shot %s' % os.path.join(SHOTS, 'bs4main_%s.bmp' % name)]
        rc, out, err = run(script, '--game', '--clock', 'Day 9 10:30')
        if rc == 0 and os.path.exists(os.path.join(SHOTS, 'bs4main_%s.bmp' % name)):
            made += 1
    check(made == len(pages), 'screenshots build/shots/bs4main_*.bmp: %d pages' % made)


# ====================================================================================================================== routes
def routes_pass():
    """the route tests through the overlays of the lungs and pumps: every (destination, ornament) pair of the 24 overlays, from the real tables"""
    bad = []
    lit = 0
    for kind, fs, base in (('lung', mo.LUNGS, 79), ('pump', mo.PUMPS, 91)):
        for k in range(base, base + 12):
            f, orn = T.overlays[k]['feature'], T.overlays[k]['ornament']
            for dest in (0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a):
                for draw in ((0, 1) if kind == 'lung' else (0,)):
                    row = [i for i, r in enumerate(T.devices) if r['first'] <= k <= r['last']][0]
                    page = T.devices[row]['page']
                    sec = patched_section(page, 1 << T.devices[row]['bit'])
                    sets = [fset(f, T.devices[row]['param'], 1), fset(f, 0x14, dest), fset(f, 0x11, draw)]
                    # all the other overlays of the row stay unset: the build stops at the first unread feature, so give the others a value
                    others = [T.overlays[j]['feature'] for j in range(T.devices[row]['first'], T.devices[row]['last'] + 1)]
                    sets += [fset(o, T.devices[row]['param'], 0) for o in others if o != f]
                    rc, out, err = run(sets + ['restore %s' % sec, 'overlays'])
                    st = parse_states(out.splitlines(), 'overlays')
                    want = 1 if (not (kind == 'lung' and draw) and 0x15 <= dest <= 0x18 and mo.lung_dest(orn) == dest) else 0
                    if st.get(k, 0) != want:
                        bad.append('%s %s dest %#x draw %d: %r want %d' % (kind, orn, dest, draw, st.get(k, 0), want))
                    lit += want
    check(not bad and lit == 24, 'route tests (lungs exe:0043b5e0, pumps exe:0043b7e0): each of the 24 overlays lights for exactly its destination (%d lit)' % lit, '; '.join(bad[:3]))


# ====================================================================================================================== main
check(mo.exe_sensor_marks()[:22] == [s['mark'] for s in T.sensors[:22]], 'the sensor mark naming rule of the oracle matches the exe table (exe:00487c40 + 0x20)')
health_pass()
overlays_pass()
routes_pass()
alarms_pass()
probes_pass()
pod_pass()
menu_pass()
mail_pass()
selftest_pass()
game_health()
game_probes()
game_probe_pictures()
game_overlays()
game_pod_menu_mail()
screenshots()
print('ALL OK' if not fails else '%d FAILED' % fails)
sys.exit(1 if fails else 0)
