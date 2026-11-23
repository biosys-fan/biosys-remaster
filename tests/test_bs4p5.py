#!/usr/bin/env python3
"""BioSys 4 terminal, packet P5 (device panels B + hint boxes): panelalgs, paneloxy, panelbubl, panelpod, panelcond, panelsub, panellock,
panelres, panelwind, panelsol and the hint boxes hsav / hoc / hevo / hrf, driven by real clicks on the real game logic (NewGame.sav).
The driver is build/biosys-bs4p5 (src/tools/bs4p5.cpp): `openpanel` = the clicks of a player (item-group hotspot, then the 0x1a hotspot),
`pstate` = the widget state of the open panel, `textchk` = the drawn text against the game's own answer. Frames: build/shots/bs4p5_*.bmp"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, 'build', 'biosys-bs4p5')
SHOTS = os.path.join(ROOT, 'build', 'shots')
os.makedirs(SHOTS, exist_ok=True)

fails = 0
passes = 0


def check(ok, what, detail=''):
    global fails, passes
    if ok:
        passes += 1
        print('PASS ' + what)
    else:
        fails += 1
        print('FAIL ' + what + (': ' + detail if detail else ''))


class Run:
    """one process, steps tagged with `echo @@ tag`"""

    def __init__(self):
        self.cmds = []

    def step(self, tag, *cmds):
        self.cmds.append('echo @@ %s' % tag)
        self.cmds.extend(cmds)
        return self

    def go(self):
        p = subprocess.run([BIN], input='\n'.join(self.cmds) + '\n', capture_output=True, text=True)
        self.rc, self.err = p.returncode, p.stderr
        self.seg = {}
        tag = None
        for l in p.stdout.splitlines():
            if l.startswith('@@ '):
                tag = l[3:]
                self.seg[tag] = []
            elif tag is not None:
                self.seg[tag].append(l)
        return self

    def lines(self, tag):
        return self.seg.get(tag, [])


def pstate(lines):
    for l in lines:
        if l.startswith('pstate panel='):
            m = re.match(r'pstate panel=(\S+) feature=\[(.*?)\] mask=(0x[0-9a-f]+) opener=(\S*) hl=(\S*)/(-?\d+) reload=(\d) reloadName=\[(.*?)\] '
                         r'handler=(\d) loop=(-?\d+) devOn=(\d) devActive=(\d) devOpen=(\d) alarm=(0x[0-9a-f]+) (.*)', l)
            d = dict(panel=m.group(1), feature=m.group(2), mask=int(m.group(3), 16), opener=m.group(4), hl=m.group(5), hlbit=int(m.group(6)),
                     reload=int(m.group(7)), reloadName=m.group(8), handler=int(m.group(9)), loop=int(m.group(10)), devOn=int(m.group(11)),
                     devActive=int(m.group(12)), devOpen=int(m.group(13)), alarm=int(m.group(14), 16), raw=l)
            rest = m.group(15)
            d['g'] = []
            for gm in re.finditer(r'g(\d)=(\S+)', rest):
                f = gm.group(2).split('|')
                d['g'].append(dict(last=float(f[0]), percent=int(f[1]), param=int(f[2]), min=float(f[3]), max=float(f[4]), image=int(f[5]),
                                   text=tuple(int(x) for x in f[6].split(',')), bar=tuple(int(x) for x in f[7].split(',')), barok=int(f[8])))
            sm = re.search(r'sched=(\d+):(\d+)-(\d+):(\d+) texts=(\S+);(\S+) markers=(\S+);(\S+) sliderY=(-?\d+) marks=(-?\d+),(-?\d+)', rest)
            d['sched'] = tuple(int(sm.group(i)) for i in range(1, 5))
            d['markers'] = (tuple(int(x) for x in sm.group(7).split(',')), tuple(int(x) for x in sm.group(8).split(',')))
            d['sliderY'] = int(sm.group(9))
            d['broken'] = int(re.search(r'broken=(-?\d+)', rest).group(1))
            return d
    return None


def gets(lines):
    """get <feature> <id> = ok int=N dbl=D lines -> {(feature, id): (int, float)}"""
    out = {}
    for l in lines:
        m = re.match(r'get (\S+) (\d+) = ok int=(-?\d+) dbl=(\S+)', l)
        if m:
            out[(m.group(1), int(m.group(2)))] = (int(m.group(3)), float(m.group(4)))
    return out


def events(lines):
    return [l for l in lines if l.startswith('set ')]


def tchk(lines):
    return [(m.group(1), int(m.group(2))) for l in lines for m in [re.match(r'textchk (\S+) (-?\d+)', l)] if m]


def percent(v, mn, mx):
    return int(((v - mn) * 100.0 / (mx - mn)) * 151.0 * 0.01)


def item(st, n):
    return bool(st['mask'] >> n & 1)


def hm(secs):
    return (secs // 3600, (secs % 3600) // 60)


def shot(name):
    return 'shot %s/bs4p5_%s.bmp' % (SHOTS, name)


def gauge_problems(st, specs, game):
    """specs [(slot, param id, min, max, feature for the get)]: slot values against the game's answer and the drawn bar"""
    bad = []
    for slot, pid, mn, mx, feat in specs:
        g = st['g'][slot]
        raw = game[(feat, pid)][1]
        want = min(max(raw, mn), mx)
        if g['param'] != pid or g['min'] != mn or g['max'] != mx:
            bad.append('slot %d: id %d [%g,%g] want %d [%g,%g]' % (slot, g['param'], g['min'], g['max'], pid, mn, mx))
        if g['last'] != want:
            bad.append('slot %d: shows %r, game says %r' % (slot, g['last'], want))
        if g['percent'] != percent(want, mn, mx):
            bad.append('slot %d: bar %d px, want %d' % (slot, g['percent'], percent(want, mn, mx)))
        if g['barok'] != 1:
            bad.append('slot %d: the bar is not drawn as image + grey' % slot)
    return bad


GAUGE_TEXT = {0: (35, 72, 65, 82), 1: (35, 112, 65, 122), 2: (35, 152, 65, 162)}


def read_cmds(specs):
    """get + text-check lines for gauge specs"""
    out = []
    for slot, pid, mn, mx, feat in specs:
        out.append('get %s %d' % (feat, pid))
    for slot, pid, mn, mx, feat in specs:
        out.append('textchk g%d @ %s %d %g %g' % (slot, feat, pid, mn, mx))
    return out


def open_expect(what, st, lines, panel, opener, feature, hl, bit, reload_name, loop, handler=1):
    bad = []
    if st is None:
        return ['no panel']
    if st['panel'] != panel:
        bad.append('panel %s' % st['panel'])
    if st['opener'] != opener:
        bad.append('opener %s' % st['opener'])
    if st['feature'] != feature:
        bad.append('feature %r' % st['feature'])
    if (st['hl'], st['hlbit']) != (hl, bit):
        bad.append('highlight %s/%d' % (st['hl'], st['hlbit']))
    if st['reloadName'] != reload_name:
        bad.append('reload name %r' % st['reloadName'])
    if st['loop'] != loop:
        bad.append('loop sound %d' % st['loop'])
    if st['handler'] != handler:
        bad.append('handler %d' % st['handler'])
    return bad


def prect(lines):
    for l in lines:
        m = re.match(r'prect (\d+),(\d+),(\d+),(\d+) items=(0x[0-9a-f]+)', l)
        if m:
            return tuple(int(m.group(i)) for i in range(1, 5))
    return None


def click_point(st, lines, name):
    """absolute screen point of the centre of panel hotspot `name`"""
    pr = prect(lines)
    for l in lines:
        m = re.match(r'phot (\S+) (-?\d+) (-?\d+) (-?\d+) (-?\d+) flags=(\d+)', l)
        if m and m.group(1) == name:
            return ((int(m.group(2)) + int(m.group(4))) // 2 + pr[0], (int(m.group(3)) + int(m.group(5))) // 2 + pr[1])
    return None


def log_problems(lines):
    return [l for l in lines if l.startswith('log ')]


def clicks_of(lines):
    for l in lines:
        m = re.match(r'sounds clicks=(\S*) on=(\S*) off=(\S*)', l)
        if m:
            return ([int(x) for x in m.group(1).split(',') if x], [int(x) for x in m.group(2).split(',') if x], [int(x) for x in m.group(3).split(',') if x])
    return ([], [], [])


def same_state(a, b):
    """the saved state of a panel: everything but the marker rectangles (a marker the original does not clear when the machine goes off)"""
    keys = ('panel', 'feature', 'mask', 'opener', 'hl', 'hlbit', 'reload', 'reloadName', 'handler', 'loop', 'devOn', 'devActive', 'devOpen', 'alarm', 'g', 'sched', 'broken')
    return all(a[k] == b[k] for k in keys)


# ===================================================================================================================== schedule machines
def schedule_machine(panel, f, mains, opener, hl, bit, loop, on_cmd_item, off_cmd_item, shots, gas=False):
    """panelalgs and panelbubl: opening, the ON buttons, the schedule slider, poll, active indicator, malfunction, close / reopen"""
    gauge = [(0, 0x78, 0, 100, f)] if gas else []
    for main in mains:
        r = Run()
        r.step('open', 'openpanel %s %s' % (main, opener), 'pstate', 'phot', 'sounds', 'logs', 'get %s 1' % f, 'get %s 3' % f, 'get %s 6' % f,
               *(read_cmds(gauge) + ['get %s 0x5a' % f] if gas else []), shot('%s_%s' % (panel, main)))
        r.go()
        L = r.lines('open')
        st, g = pstate(L), gets(L)
        bad = open_expect(panel, st, L, panel, opener, f, hl, bit, '', loop)
        on, ln = g[(f, 3)][0], g[(f, 6)][0]
        off = on + ln - (86400 if on + ln > 86400 else 0)
        if st:
            if st['devOn'] != g[(f, 1)][0] or item(st, 1) != bool(g[(f, 1)][0]) or item(st, 2) == bool(g[(f, 1)][0]):
                bad.append('on/off items %s devOn %d (game ON %d)' % (hex(st['mask']), st['devOn'], g[(f, 1)][0]))
            if st['sched'] != hm(on) + hm(off):
                bad.append('schedule %r want %r' % (st['sched'], hm(on) + hm(off)))
            if st['broken'] == 1:
                bad.append('broken picture shown')
            if gas:
                src = g[(f, 0x5a)][0]
                if item(st, 0x11) != (src == 0x5b) or item(st, 0x12) != (src == 0x5c):
                    bad.append('gas source %#x but items %s' % (src, hex(st['mask'])))
                bad += gauge_problems(st, gauge, g)
                bad += ['text of the gauge not as drawn' for k, v in tchk(L) if v != 1]
        bad += log_problems(L)
        check(not bad, '%s opened from %s by real clicks: feature, highlight %s/%d, items follow FEATURE_ON%s, schedule %d:%02d-%d:%02d from the game, loop sound %d' %
              (panel, main, hl, bit, ', gas source, oxygen gauge' if gas else '', hm(on)[0], hm(on)[1], hm(off)[0], hm(off)[1], loop), '; '.join(bad))
    # the ON buttons and the markers
    r = Run()
    r.step('open', 'openpanel %s %s' % (mains[0], opener), 'pstate', 'phot')
    r.step('on', 'events', 'clickh %s' % on_cmd_item, 'events', 'pstate', 'get %s 1' % f, 'textchk on @ %s' % f, 'textchk off @ %s' % f, 'logs', shot('%s_on' % panel))
    r.go()
    L0, L1 = r.lines('open'), r.lines('on')
    s0, s1 = pstate(L0), pstate(L1)
    check(events(L1) == ['set %s 1 1 0 0' % f] and gets(L1)[(f, 1)][0] == 1 and s1['devOn'] == 1 and item(s1, 1) and not item(s1, 2),
          '%s: a real click on %s sends set FEATURE_ON = 1 and the game turns the machine on (items swap)' % (panel, on_cmd_item), '%r %r' % (events(L1), s1['raw'][:80]))
    check(all(v == 1 for k, v in tchk(L1)) and len(tchk(L1)) == 2 and s1['markers'][0][0] != -1 and s1['markers'][1][0] != -1,
          '%s: with the machine on the slider shows both markers and the on / off times of the game as text' % panel, repr(tchk(L1)))
    # drag the on-marker 12 px (2 h) to the right
    pr = prect(L0)
    mk = s1['markers'][0]
    y, x = pr[1] + (mk[1] + mk[3]) // 2, pr[0] + mk[0] + 2
    gon = gets(r.lines('open')).get((f, 3))
    r = Run()
    r.step('open', 'openpanel %s %s' % (mains[0], opener), 'get %s 3' % f, 'get %s 6' % f, 'clickh %s' % on_cmd_item, 'events')
    r.step('drag', 'down %d %d' % (x, y), 'move %d %d' % (x + 6, y), 'move %d %d' % (x + 12, y), 'up %d %d' % (x + 12, y), 'events', 'pstate', 'get %s 3' % f,
           'get %s 6' % f, 'textchk on @ %s' % f, 'textchk off @ %s' % f, 'logs', shot('%s_drag' % panel))
    r.go()
    g0, L = gets(r.lines('open')), r.lines('drag')
    st, g = pstate(L), gets(L)
    on0, len0 = g0[(f, 3)][0], g0[(f, 6)][0]
    off0 = (on0 + len0) % 86400 if on0 + len0 > 86400 else on0 + len0
    x0 = mk[0]
    on_s = (x0 + 12) * 600 - 0x3840
    dur = off0 - on_s + (86400 if off0 < on_s else 0)
    check(events(L) == ['set %s 3 %d 0 0' % (f, on_s), 'set %s 6 %d 0 0' % (f, dur)] and g[(f, 3)][0] == on_s and g[(f, 6)][0] == dur and st['sched'] == hm(on_s) + hm(off0),
          '%s: dragging the on-marker 2 h sends TIME_ACTIVATE %d and DAILY_TIME_LIMIT %d, the game keeps them, the slider shows %d:%02d / %d:%02d' % (
              panel, on_s, dur, hm(on_s)[0], hm(on_s)[1], hm(off0)[0], hm(off0)[1]), '%r %r %r' % (events(L), g, st['sched']))
    check(all(v == 1 for k, v in tchk(L)) and len(tchk(L)) == 2, '%s: the dragged times are drawn as the game reports them' % panel, repr(tchk(L)))
    # drag the off-marker 6 px (1 h) to the right: the on time is written again, the length grows by 1 h
    mk1 = s1['markers'][1]
    y1, x1 = pr[1] + (mk1[1] + mk1[3]) // 2, pr[0] + mk1[2] - 2
    r = Run()
    r.step('open', 'openpanel %s %s' % (mains[0], opener), 'get %s 3' % f, 'get %s 6' % f, 'clickh %s' % on_cmd_item, 'events')
    r.step('drag', 'down %d %d' % (x1, y1), 'move %d %d' % (x1 + 3, y1), 'move %d %d' % (x1 + 6, y1), 'up %d %d' % (x1 + 6, y1), 'events', 'pstate', 'get %s 3' % f,
           'get %s 6' % f, 'textchk on @ %s' % f, 'textchk off @ %s' % f, 'logs')
    r.go()
    g0, L = gets(r.lines('open')), r.lines('drag')
    st, g = pstate(L), gets(L)
    on0, len0 = g0[(f, 3)][0], g0[(f, 6)][0]
    off_s = (mk1[0] + 6) * 600 - 0x3840
    dur = off_s - on0 + (86400 if off_s < on0 else 0)
    check(events(L) == ['set %s 3 %d 0 0' % (f, on0), 'set %s 6 %d 0 0' % (f, dur)] and g[(f, 3)][0] == on0 and g[(f, 6)][0] == dur and st['sched'] == hm(on0) + hm(off_s) and
          all(v == 1 for k, v in tchk(L)),
          '%s: dragging the off-marker 1 h sends TIME_ACTIVATE %d (unchanged) and DAILY_TIME_LIMIT %d, the slider shows %d:%02d - %d:%02d' % (
              panel, on0, dur, hm(on0)[0], hm(on0)[1], hm(off_s)[0], hm(off_s)[1]), '%r %r' % (events(L), st['sched']))
    # turn off, close, reopen
    r = Run()
    r.step('open', 'openpanel %s %s' % (mains[0], opener), 'clickh %s' % on_cmd_item, 'events', 'clickh %s' % off_cmd_item, 'events', 'pstate', 'get %s 1' % f)
    r.step('again', 'close', 'openpanel %s %s' % (mains[0], opener), 'pstate', 'get %s 1' % f)
    r.go()
    a, b = r.lines('open'), r.lines('again')
    ev = events(a)
    check(ev == ['set %s 1 1 0 0' % f, 'set %s 1 0 0 0' % f] and gets(a)[(f, 1)][0] == 0 and same_state(pstate(a), pstate(b)),
          '%s: %s sends FEATURE_ON = 0; closing and reopening the panel gives the same state' % (panel, off_cmd_item), '%r' % ev)
    # the 1 Hz poll: ON changes in the game behind the panel's back
    r = Run()
    r.step('open', 'openpanel %s %s' % (mains[0], opener), 'sounds')
    r.step('poll', 'setp %s 1 1' % f, 'advance 400', 'pstate', 'advance 1200', 'pstate', 'get %s 1' % f, 'logs')
    r.step('act', 'ovr %s 2 1 0' % f, 'advance 1500', 'pstate', 'sounds', shot('%s_active' % panel))
    r.step('idle', 'ovr %s 2 0 0' % f, 'advance 1500', 'pstate', 'sounds')
    r.go()
    pl = [l for l in r.lines('poll') if l.startswith('pstate')]
    pa, pi = pstate(r.lines('act')), pstate(r.lines('idle'))
    st_a = (re.search(r'devOn=(\d)', pl[0]).group(1), re.search(r'devOn=(\d)', pl[1]).group(1))
    check(st_a == ('0', '1') and gets(r.lines('poll'))[(f, 1)][0] == 1,
          '%s: a change of FEATURE_ON in the game shows after the next once-per-second poll (the panel reloads), not before' % panel, repr(st_a))
    ca, ci = clicks_of(r.lines('act')), clicks_of(r.lines('idle'))
    check(pa['devActive'] == 1 and item(pa, 25) and loop in ca[1] and pi['devActive'] == 0 and not item(pi, 25) and loop in ci[2],
          '%s: FEATURE_ACTIVE shows the active picture (item 0x19) and starts / stops loop sound %d' % (panel, loop), '%s %s %s' % (ca, ci, pa['raw'][:90]))
    # broken
    r = Run()
    r.step('open', 'ovr %s 7 1 0' % f, 'openpanel %s %s' % (mains[0], opener), 'pstate', 'sounds', 'logs', shot('%s_broken' % panel))
    r.go()
    st = pstate(r.lines('open'))
    cl = clicks_of(r.lines('open'))
    check(st['broken'] == 1 and st['handler'] == 0 and not item(st, 1) and not item(st, 2) and not item(st, 15) and 7 in cl[0],
          '%s: FEATURE_MALFUNCTION shows the broken picture, hides the controls, leaves no handler and plays click 7' % panel, '%s %s' % (st['raw'][:100], cl))


def test_algs():
    schedule_machine('panelalgs', 'fbo3algae', ['watpur', 'ocalg'], '7_algo1', 'hialg', 7, 3, '2_turnon', '1_turnoff', None)


def test_bubl():
    f = 'fw3bubl'
    schedule_machine('panelbubl', f, ['ocoxy', 'air'], '6_bubo1', 'hibub', 6, 5, '2_activate', '1_stop', None, gas=True)
    # gas source buttons
    r = Run()
    r.step('open', 'openpanel ocoxy 6_bubo1', 'get %s 0x5a' % f, 'pstate')
    r.step('oxy', 'events', 'clickh 15_oxyon', 'events', 'pstate', 'get %s 0x5a' % f, 'logs', shot('panelbubl_gas_oxygen'))
    r.step('air', 'clickh 16_airon', 'events', 'pstate', 'get %s 0x5a' % f, 'logs', shot('panelbubl_gas_air'))
    r.go()
    s0 = pstate(r.lines('open'))
    so, sa = pstate(r.lines('oxy')), pstate(r.lines('air'))
    g0, go, ga = gets(r.lines('open')), gets(r.lines('oxy')), gets(r.lines('air'))
    check(g0[(f, 0x5a)][0] == 0x5c and events(r.lines('oxy')) == ['set %s 90 91 0 0' % f] and go[(f, 0x5a)][0] == 0x5b and item(so, 0x11) and not item(so, 0x12) and
          events(r.lines('air')) == ['set %s 90 92 0 0' % f] and ga[(f, 0x5a)][0] == 0x5c and item(sa, 0x12) and not item(sa, 0x11),
          'panelbubl: 15_oxyon sends FEATURE_GAS_SOURCE 0x5b and 16_airon 0x5c, the game keeps them and the selected picture (items 0x11 / 0x12) follows',
          '%r %r' % (events(r.lines('oxy')), events(r.lines('air'))))
    # polls: the gas source changes in the game; the oxygen gauge changes
    r = Run()
    r.step('open', 'openpanel ocoxy 6_bubo1')
    r.step('poll', 'setp %s 0x5a 0x5b' % f, 'advance 400', 'pstate', 'advance 1200', 'pstate', 'get %s 0x5a' % f)
    r.step('gauge0', 'ovr %s 0x78 0 37.55' % f, 'advance 400', 'pstate', 'advance 1200', 'pstate', *read_cmds([(0, 0x78, 0, 100, f)]), shot('panelbubl_oxygen'))
    r.step('gauge1', 'ovr %s 0x78 0 100.0' % f, 'advance 1500', 'pstate', *read_cmds([(0, 0x78, 0, 100, f)]))
    r.step('gauge2', 'ovr %s 0x78 0 140.0' % f, 'advance 1500', 'pstate', 'logs', *read_cmds([(0, 0x78, 0, 100, f)]))
    r.go()
    pl = [pstate([l]) for l in r.lines('poll') if l.startswith('pstate')]
    check(item(pl[0], 0x12) and not item(pl[0], 0x11) and item(pl[1], 0x11) and not item(pl[1], 0x12),
          'panelbubl: a change of FEATURE_GAS_SOURCE in the game shows after the next poll (items 0x12 -> 0x11), not before')
    p0 = [pstate([l]) for l in r.lines('gauge0') if l.startswith('pstate')]
    g = gets(r.lines('gauge0'))
    bad = gauge_problems(p0[1], [(0, 0x78, 0, 100, f)], g)
    check(p0[0]['g'][0]['last'] == 0 and not bad and all(v == 1 for k, v in tchk(r.lines('gauge0'))),
          'panelbubl: the oxygen-store gauge (0x78) follows the game after a poll (0 -> 37.55: %d px, text drawn)' % p0[1]['g'][0]['percent'], '; '.join(bad))
    p1 = pstate(r.lines('gauge1'))
    check(not gauge_problems(p1, [(0, 0x78, 0, 100, f)], gets(r.lines('gauge1'))) and p1['g'][0]['percent'] == 151 and all(v == 1 for k, v in tchk(r.lines('gauge1'))),
          'panelbubl: a full gauge (100) is a 151 px bar with the text "100"')
    p2 = pstate(r.lines('gauge2'))
    check(p2['g'][0]['last'] == 100.0 and p2['g'][0]['percent'] == 151 and all(v == 1 for k, v in tchk(r.lines('gauge2'))),
          'panelbubl: a value above the maximum (140) is clamped to 100 by the gauge', repr(p2['g'][0]))
    # the invalid gas source value is refused at the init
    r = Run()
    r.step('bad', 'ovr %s 0x5a 7 0' % f, 'openpanel ocoxy 6_bubo1', 'pstate', 'logs')
    r.go()
    st = pstate(r.lines('bad'))
    check(any("Invalid value (7) of parameter FEATURE_GAS_SOURCE for BiosFear feature 'fw3bubl'" in l for l in r.lines('bad')) and st['handler'] == 0,
          'panelbubl: an invalid FEATURE_GAS_SOURCE value is logged and the init stops (no handler)')


# ===================================================================================================================== paneloxy / panelpod / panellock
def test_oxy():
    f = 'fbo4oxygen'
    specs = [(0, 0x22, 0, 100, f), (1, 0x78, 0, 100, f)]
    for main in ('oxyair', 'air'):
        r = Run()
        r.step('open', 'openpanel %s 3_oxy1' % main, 'pstate', 'phot', 'sounds', 'logs', 'get %s 2' % f, *read_cmds(specs), shot('paneloxy_%s' % main))
        r.go()
        L = r.lines('open')
        st, g = pstate(L), gets(L)
        bad = open_expect('oxy', st, L, 'paneloxy', '3_oxy1', f, 'hioxy', 3, '', 3)
        if st:
            act = g[(f, 2)][0]
            if st['devActive'] != act or item(st, 1) == bool(act) or item(st, 2) != bool(act):
                bad.append('active items %s devActive %d (game %d)' % (hex(st['mask']), st['devActive'], act))
            bad += gauge_problems(st, specs, g)
            bad += ['gauge text mismatch' for k, v in tchk(L) if v != 1]
            for slot in (0, 1):
                if st['g'][slot]['text'] != GAUGE_TEXT[slot]:
                    bad.append('text rect %r' % (st['g'][slot]['text'],))
        bad += log_problems(L)
        check(not bad, 'paneloxy opened from %s by real clicks: feature, highlight hioxy/3, FEATURE_ACTIVE items, the oxygen (0x22) and stored-oxygen (0x78) gauges and their text '
              'show the game values' % main, '; '.join(bad))
    r = Run()
    r.step('open', 'openpanel oxyair 3_oxy1', 'pstate', 'phot', 'sounds')
    r.step('go', 'events', 'clickh 1_activate', 'events', 'pstate', 'sounds', 'logs', shot('paneloxy_active'))
    r.step('stop', 'clickh 2_stop', 'events', 'pstate', 'sounds', 'logs')
    r.step('again', 'close', 'openpanel oxyair 3_oxy1', 'pstate')
    r.go()
    a, b = pstate(r.lines('go')), pstate(r.lines('stop'))
    ev1, ev2 = events(r.lines('go')), events(r.lines('stop'))
    c1, c2 = clicks_of(r.lines('go')), clicks_of(r.lines('stop'))
    check(ev1 == ['set %s 2 1 0 0' % f] and a['devActive'] == 1 and item(a, 2) and not item(a, 1) and 3 in c1[1] and
          ev2 == ['set %s 2 0 0 0' % f] and b['devActive'] == 0 and item(b, 1) and not item(b, 2) and 3 in c2[2] and same_state(b, pstate(r.lines('again'))),
          'paneloxy: 1_activate sends FEATURE_ACTIVE = 1 (loop sound 3 on), 2_stop sends 0 (off); reopening shows the same state', '%r %r' % (ev1, ev2))
    # polls
    r = Run()
    r.step('open', 'openpanel oxyair 3_oxy1')
    r.step('g', 'ovr %s 0x22 0 55.55' % f, 'ovr %s 0x78 0 12.25' % f, 'advance 400', 'pstate', 'advance 1200', 'pstate', *read_cmds(specs), shot('paneloxy_gauges'))
    r.step('act', 'ovr %s 2 1 0' % f, 'advance 400', 'pstate', 'advance 1200', 'pstate', 'get %s 2' % f, 'sounds')
    r.go()
    pg = [pstate([l]) for l in r.lines('g') if l.startswith('pstate')]
    bad = gauge_problems(pg[1], specs, gets(r.lines('g')))
    check(pg[0]['g'][0]['last'] != 55.55 and not bad and pg[1]['g'][0]['last'] == 55.55 and pg[1]['g'][1]['last'] == 12.25 and all(v == 1 for k, v in tchk(r.lines('g'))),
          'paneloxy: both gauges follow the game after the poll, not before (55.55 / 12.25)', '; '.join(bad))
    pa = [pstate([l]) for l in r.lines('act') if l.startswith('pstate')]
    check(pa[0]['devActive'] == 0 and pa[1]['devActive'] == 1 and item(pa[1], 2) and not item(pa[1], 1),
          'paneloxy: a change of FEATURE_ACTIVE in the game reloads the panel at the next poll (item 2 shown)')


def test_pod():
    f = 'fPod'
    positions = {0x15: 'podrf', 0x16: 'podsav', 0x17: 'podevo', 0x18: 'podoc', 0x19: 'podhouse'}
    specs = [(0, 0x8d, 0, 100, f)]
    for pos, name in sorted(positions.items()):
        r = Run()
        r.step('open', 'ovr fPod 0x85 %d 0' % pos, 'openpanel pod %s' % name, 'pstate', 'phot', 'sounds', 'logs', 'get %s 0x86' % f, *read_cmds(specs), shot('panelpod_%s' % name))
        r.go()
        L = r.lines('open')
        st, g = pstate(L), gets(L)
        bad = open_expect('pod', st, L, 'panelpod', name, f, '', 0, '', -1)
        if st:
            ch = g[(f, 0x86)][0]
            if st['devActive'] != (1 if ch else 0) or item(st, 1) == bool(ch) or item(st, 2) != bool(ch):
                bad.append('items %s devActive %d (game %d)' % (hex(st['mask']), st['devActive'], ch))
            bad += gauge_problems(st, specs, g)
            bad += ['gauge text mismatch' for k, v in tchk(L) if v != 1]
            if st['g'][0]['text'] != GAUGE_TEXT[0]:
                bad.append('text rect')
        bad += log_problems(L)
        check(not bad, 'panelpod opened by a real click on %s (pod position %#x): FEATURE_START_CHARGING_POD items, the power-available gauge (0x8d) and its text show the game values' % (name, pos),
              '; '.join(bad))
    r = Run()
    r.step('open', 'ovr fPod 0x85 0x15 0', 'openpanel pod podrf', 'pstate', 'phot')
    r.step('go', 'events', 'clickh 1_activate', 'events', 'pstate', 'get %s 0x86' % f, 'logs', shot('panelpod_charging'))
    r.step('stop', 'clickh 2_stop', 'events', 'pstate', 'get %s 0x86' % f, 'logs')
    r.step('again', 'close', 'openpanel pod podrf', 'pstate')
    r.go()
    a, b = pstate(r.lines('go')), pstate(r.lines('stop'))
    ev1, ev2 = events(r.lines('go')), events(r.lines('stop'))
    g1, g2 = gets(r.lines('go')), gets(r.lines('stop'))
    check(ev1 == ['set %s 143 1 0 0' % f] and a['devActive'] == 1 and item(a, 2) and not item(a, 1) and
          ev2 == ['set %s 144 1 0 0' % f] and b['devActive'] == 0 and item(b, 1) and not item(b, 2) and same_state(b, pstate(r.lines('again'))),
          'panelpod: 1_activate sends 0x8f = 1 (start charging), 2_stop sends 0x90 = 1 (stop); reopening shows the same state', '%r %r' % (ev1, ev2))
    check(g1[(f, 0x86)][0] == 1 and g2[(f, 0x86)][0] == 0, 'panelpod: the game applies both (FEATURE_START_CHARGING_POD 0 -> 1 -> 0)', '%r %r' % (g1, g2))
    r = Run()
    r.step('open', 'ovr fPod 0x85 0x15 0', 'openpanel pod podrf')
    r.step('g', 'ovr %s 0x8d 0 63.65' % f, 'advance 400', 'pstate', 'advance 1200', 'pstate', *read_cmds(specs))
    r.step('ch', 'setp %s 0x8f 1' % f, 'advance 400', 'pstate', 'advance 1200', 'pstate', 'get %s 0x86' % f)
    r.go()
    pg = [pstate([l]) for l in r.lines('g') if l.startswith('pstate')]
    bad = gauge_problems(pg[1], specs, gets(r.lines('g')))
    check(pg[0]['g'][0]['last'] != 63.65 and not bad and all(v == 1 for k, v in tchk(r.lines('g'))), 'panelpod: the power gauge follows the game after the poll (63.65)', '; '.join(bad))
    pc = [pstate([l]) for l in r.lines('ch') if l.startswith('pstate')]
    check(pc[0]['devActive'] == 0 and pc[1]['devActive'] == 1 and item(pc[1], 2) and gets(r.lines('ch'))[(f, 0x86)][0] == 1,
          'panelpod: a change of the charging state in the game reloads the panel at the next poll')


LOCKS = [('18_locks1', 'fspseal', 'hiseals1', 3), ('18_locks2', 'fsoseal', 'hiseals2', 4), ('18_locko1', 'fopseal', 'hisealo1', 6),
         ('18_locke1', 'fdpseal', 'hiseale1', 9), ('18_locke2', 'ft3seal', 'hiseale2', 10), ('18_lockr1', 'frpseal', 'hisealr1', 12)]


def test_lock():
    for main in ('pollut', 'air'):
        for opener, f, hl, var in LOCKS:
            r = Run()
            r.step('open', 'openpanel %s %s' % (main, opener), 'pstate', 'logs', 'get %s 10' % f, shot('panellock_%s_%s' % (main, opener[3:])) if main == 'pollut' else 'echo -')
            r.go()
            L = r.lines('open')
            st, g = pstate(L), gets(L)
            bad = open_expect('lock', st, L, 'panellock', opener, f, hl, 0x12, opener, -1)
            if st:
                op = g[(f, 10)][0]
                if st['devOpen'] != op or item(st, 1) != bool(op) or item(st, 2) == bool(op) or not item(st, var):
                    bad.append('seal items %s devOpen %d (game %d)' % (hex(st['mask']), st['devOpen'], op))
            bad += log_problems(L)
            check(not bad, 'panellock opened from %s by a real click on %s (variant item %d): feature %s, highlight %s/18, FEATURE_OPEN picture' % (main, opener, var, f, hl), '; '.join(bad))
    # open / closed pictures follow FEATURE_OPEN of the game
    r = Run()
    r.step('open', 'ovr fspseal 10 1 0', 'openpanel pollut 18_locks1', 'pstate', shot('panellock_open'))
    r.go()
    st = pstate(r.lines('open'))
    check(st['devOpen'] == 1 and item(st, 1) and not item(st, 2), 'panellock: FEATURE_OPEN = 1 shows item 1 (open) and hides item 2')
    # the variant tabs only change the picture (the original does not call the init again): no events, same feature
    r = Run()
    r.step('open', 'openpanel pollut 18_locks1', 'pstate', 'phot', 'events')
    r.go()
    L = r.lines('open')
    tab = [l for l in L if l.startswith('phot hotspot') and 'verb=17' in l][1]
    m = re.match(r'phot hotspot (\d+) (\d+) (\d+) (\d+)', tab)
    pr = prect(L)
    cx, cy = pr[0] + (int(m.group(1)) + int(m.group(3))) // 2, pr[1] + (int(m.group(2)) + int(m.group(4))) // 2
    r2 = Run()
    r2.step('open', 'openpanel pollut 18_locks1', 'events', 'click %d %d' % (cx, cy), 'events', 'pstate', 'logs')
    r2.go()
    st2 = pstate(r2.lines('open'))
    check(not events(r2.lines('open')) and item(st2, 4) and not item(st2, 3) and st2['feature'] == 'fspseal',
          'panellock: a click on the second variant tab shows item 4 and sends no event (the init is not run again: ORIGINAL behaviour, the feature stays fspseal)', st2['raw'][:100])
    # the setter: items 1 / 2 changing through verb 0x17 (no hotspot of the page does it); the fake table plays the game here; the real seals are in tests/test_bs4fixes.py
    r = Run()
    r.step('s', 'openpanel pollut 18_locks1', 'fake on', 'events', 'act 17 0 0 2 0 0 4', 'events', 'pstate', 'act 17 0 0 4 0 0 2', 'events', 'pstate', 'logs')
    r.go()
    L = r.lines('s')
    ps = [pstate([l]) for l in L if l.startswith('pstate')]
    ev = events(L)
    check(ev == ['set fspseal 10 1 0 0', 'set fspseal 10 0 0 0'] and ps[0]['devOpen'] == 1 and ps[1]['devOpen'] == 0,
          'panellock: showing item 1 sends FEATURE_OPEN = 1 and showing item 2 sends 0 (setter exe:004326e0), the panel keeps devOpen', repr(ev))
    r = Run()
    r.step('s', 'openpanel pollut 18_locks1', 'events', 'act 17 0 0 2 0 0 4', 'events', 'pstate', 'logs')
    r.go()
    L = r.lines('s')
    st = pstate(L)
    why = 'applied' if st['devOpen'] else 'refused (%s)' % ' '.join(l[4:] for l in L if l.startswith('log '))[:80]
    check(events(L) == ['set fspseal 10 1 0 0'], 'panellock: the same through the real game logic: the event reaches it (result of the game: %s)' % why)


# ===================================================================================================================== cond / sub
COND = [('13_conds1', 'fbs4cond', 'hiconds1', 3), ('13_condo1', 'fbo3cond', 'hicondo1', 6), ('13_conde1', 'fbd3cond', 'hiconde1', 9), ('13_condr1', 'fbr3cond', 'hicondr1', 12)]
SUB = [('4_subs1', 'fssub1', 'hisubs1', 3), ('4_subs2', 'fssub2', 'hisubs2', 4), ('4_subs3', 'fssub3', 'hisubs3', 5), ('4_sube1', 'fdbsub1', 'hisube1', 9),
       ('4_sube2', 'fdbsub2', 'hisube2', 10), ('4_sube3', 'fdbsub3', 'hisube3', 11), ('4_subr1', 'frfsub1', 'hisubr1', 12), ('4_subr2', 'frfsub2', 'hisubr2', 13),
       ('4_subr3', 'frfsub3', 'hisubr3', 14)]


def tank_panel(panel, table, bit, mains):
    for main in mains:
        for opener, f, hl, var in table:
            specs = [(0, 0x2b, 0, 100, f), (1, 0x28, -10, 40, f), (2, 0x2c, 0, 100, f)]
            r = Run()
            r.step('open', 'openpanel %s %s' % (main, opener), 'pstate', 'sounds', 'logs', 'get %s 7' % f, *read_cmds(specs), 'get %s 0x32' % f, shot('%s_%s' % (panel, opener.split('_')[1])) if main == mains[0] else 'echo -')
            r.go()
            L = r.lines('open')
            st, g = pstate(L), gets(L)
            bad = open_expect(panel, st, L, panel, opener, f, hl, bit, opener, -1)
            broken = g[(f, 7)][0] != 0
            if st and broken:           # the dead-biome machines of NewGame.sav are broken: the broken picture, no gauges, no handler, click 7
                if st['broken'] != 1 or st['handler'] != 0 or 7 not in clicks_of(L)[0] or st['g'][0]['param'] != 0:
                    bad = [b for b in bad if not b.startswith('handler')] + ['broken machine: picture %d, handler %d, clicks %r' % (st['broken'], st['handler'], clicks_of(L)[0])]
                else:
                    bad = [b for b in bad if not b.startswith('handler')]
            elif st:
                if not item(st, var):
                    bad.append('variant item %d not shown' % var)
                bad += gauge_problems(st, specs, g)
                bad += ['gauge text mismatch' for k, v in tchk(L) if v != 1]
                for slot in range(3):
                    if st['g'][slot]['text'] != GAUGE_TEXT[slot]:
                        bad.append('text rect %r' % (st['g'][slot]['text'],))
                if panel == 'panelcond' and st['alarm'] != g[(f, 0x32)][0]:
                    bad.append('alarm bits %#x' % st['alarm'])
            bad += log_problems(L)
            check(not bad, '%s opened from %s by a real click on %s: feature %s, highlight %s/%d, %s' %
                  (panel, main, opener, f, hl, bit, 'the machine is broken in the game: broken picture, no gauges, no handler, click 7' if broken else 'three gauges (level 0x2b, temperature 0x28, purity 0x2c) with the game values'), '; '.join(bad))


def tank_polls(panel, opener, f, main='watpur'):
    specs = [(0, 0x2b, 0, 100, f), (1, 0x28, -10, 40, f), (2, 0x2c, 0, 100, f)]
    r = Run()
    r.step('open', 'openpanel %s %s' % (main, opener), 'pstate', *read_cmds(specs))
    r.step('t', 'tank %s temp 33.25' % f, 'tank %s purity 0.5' % f, 'tank %s water 20' % f, 'advance 400', 'pstate', 'advance 1200', 'pstate', *read_cmds(specs), shot('%s_changed' % panel))
    r.step('x', 'tank %s temp -30' % f, 'advance 1500', 'pstate', 'logs', *read_cmds(specs))
    r.go()
    p0 = pstate(r.lines('open'))
    pt = [pstate([l]) for l in r.lines('t') if l.startswith('pstate')]
    g = gets(r.lines('t'))
    bad = gauge_problems(pt[1], specs, g)
    changed = [pt[1]['g'][i]['last'] != p0['g'][i]['last'] for i in range(3)]
    check(not bad and all(changed) and pt[0]['g'][1]['last'] == p0['g'][1]['last'] and all(v == 1 for k, v in tchk(r.lines('t'))),
          '%s: after the tank changes in the game (temperature, purity, volume) all three gauges and their texts follow after the poll, not before' % panel, '; '.join(bad) + repr(changed))
    px = pstate(r.lines('x'))
    check(not gauge_problems(px, specs, gets(r.lines('x'))) and px['g'][1]['last'] == -10 and px['g'][1]['percent'] == 0 and all(v == 1 for k, v in tchk(r.lines('x'))),
          '%s: a temperature below the gauge minimum (-30) is clamped to -10 (0 px)' % panel)


def test_cond():
    tank_panel('panelcond', COND, 13, ['watpur'])
    tank_polls('panelcond', '13_conds1', 'fbs4cond')
    # the alarm icon of the purity bar: FEATURE_ALARMS & 0x8000
    r = Run()
    r.step('open', 'openpanel watpur 13_conds1', 'pstate')
    r.step('al', 'ovr fbs4cond 0x32 0x8000 0', 'advance 1500', 'pstate', shot('panelcond_alarm'))
    r.step('al2', 'ovr fbs4cond 0x32 0x2 0', 'advance 1500', 'pstate')
    r.go()
    a = pstate(r.lines('al'))
    b = pstate(r.lines('al2'))
    check(pstate(r.lines('open'))['alarm'] == 0 and a['alarm'] & 0x8000 == 0x8000 and b['alarm'] & 0x8000 == 0,
          'panelcond: FEATURE_ALARMS bit 0x8000 raises the alarm icon at the purity bar after the poll (and not the other bits)', '%#x %#x' % (a['alarm'], b['alarm']))
    r = Run()
    r.step('b', 'ovr fbs4cond 7 1 0', 'openpanel watpur 13_conds1', 'pstate', 'sounds', shot('panelcond_broken'))
    r.go()
    st = pstate(r.lines('b'))
    check(st['broken'] == 1 and st['handler'] == 0 and 7 in clicks_of(r.lines('b'))[0] and st['g'][0]['param'] == 0,
          'panelcond: FEATURE_MALFUNCTION shows the broken picture, sets up no gauge and no handler, click 7', st['raw'][:100])


def test_sub():
    tank_panel('panelsub', SUB, 4, ['watpur', 'swc'])
    tank_polls('panelsub', '4_subs2', 'fssub2')


# ===================================================================================================================== sol / wind / res
def test_power():
    for panel, main, opener, f, hl, bit in (('panelsol', 'power', '4_sol', 'fsolargen', 'hisol', 4), ('panelwind', 'power', '3_wind', 'fwindmill', 'hiwind', 3)):
        specs = [(0, 0x8c, 0, 100, f)]
        r = Run()
        r.step('open', 'openpanel %s %s' % (main, opener), 'pstate', 'sounds', 'logs', *read_cmds(specs), shot(panel))
        r.go()
        L = r.lines('open')
        st, g = pstate(L), gets(L)
        bad = open_expect(panel, st, L, panel, opener, f, hl, bit, '', 4)
        if st:
            bad += gauge_problems(st, specs, g)
            bad += ['gauge text mismatch' for k, v in tchk(L) if v != 1]
            if st['g'][0]['text'] != GAUGE_TEXT[0]:
                bad.append('text rect')
            if 4 not in clicks_of(L)[1]:
                bad.append('loop sound 4 not started')
        bad += log_problems(L)
        check(not bad, '%s opened by a real click on %s: feature %s, highlight %s/%d, the power-generation gauge (0x8c) and its text show the game value %.3f, loop sound 4' %
              (panel, opener, f, hl, bit, g[(f, 0x8c)][1]), '; '.join(bad))
        r = Run()
        r.step('open', 'openpanel %s %s' % (main, opener), 'pstate')
        r.step('c', 'hdr sun 0.3', 'hdr wind 6.5', 'upd %s' % f, 'advance 400', 'pstate', 'advance 1200', 'pstate', 'get %s 0x8c' % f, *read_cmds(specs), shot(panel + '_changed'))
        r.go()
        p0 = pstate(r.lines('open'))
        pc = [pstate([l]) for l in r.lines('c') if l.startswith('pstate')]
        g = gets(r.lines('c'))
        bad = gauge_problems(pc[1], specs, g)
        check(not bad and pc[0]['g'][0]['last'] == p0['g'][0]['last'] and pc[1]['g'][0]['last'] != p0['g'][0]['last'] and all(v == 1 for k, v in tchk(r.lines('c'))),
              '%s: the gauge follows the game after the poll (%.3f -> %.3f), not before' % (panel, p0['g'][0]['last'], pc[1]['g'][0]['last']), '; '.join(bad))
    # panelres: generation / usage of the whole world
    specs = [(0, 0x3f2, 0, 200, '-'), (1, 0x3f4, 0, 200, '-')]
    r = Run()
    r.step('open', 'openpanel power 1_res', 'pstate', 'sounds', 'logs', *read_cmds(specs), shot('panelres'))
    r.step('c', 'hdr gen 77.7', 'hdr demand 31.25', 'advance 400', 'pstate', 'advance 1200', 'pstate', *read_cmds(specs), shot('panelres_changed'))
    r.step('hi', 'hdr gen 250', 'hdr demand 0', 'advance 1500', 'pstate', 'logs', *read_cmds(specs))
    r.go()
    L = r.lines('open')
    st, g = pstate(L), gets(L)
    bad = open_expect('res', st, L, 'panelres', '1_res', '', 'hires', 1, '', 4)
    bad += gauge_problems(st, specs, g) + ['gauge text mismatch' for k, v in tchk(L) if v != 1] + log_problems(L)
    if st['g'][0]['text'] != (28, 72, 67, 82) or st['g'][1]['text'] != (35, 122, 65, 132):
        bad.append('text rects %r' % ([st['g'][0]['text'], st['g'][1]['text']],))
    check(not bad, 'panelres opened by a real click on 1_res: NULL feature, highlight hires/1, generation (0x3f2) and usage (0x3f4) gauges with range 200 show the game values', '; '.join(bad))
    pc = [pstate([l]) for l in r.lines('c') if l.startswith('pstate')]
    bad = gauge_problems(pc[1], specs, gets(r.lines('c')))
    check(not bad and pc[0]['g'][0]['last'] == st['g'][0]['last'] and pc[1]['g'][0]['last'] == 77.7 and abs(pc[1]['g'][1]['last'] - 31.25) < 1e-9 and all(v == 1 for k, v in tchk(r.lines('c'))),
          'panelres: after the world power changes both gauges follow after the poll (77.7 / 31.25), not before', '; '.join(bad))
    ph = pstate(r.lines('hi'))
    check(ph['g'][0]['last'] == 200 and ph['g'][0]['percent'] == 151 and ph['g'][1]['last'] == 0.0001 and all(v == 1 for k, v in tchk(r.lines('hi'))),
          'panelres: the generation is clamped at 200 (the game clamps the usage to 0.0001 at least)', repr([ph['g'][0]['last'], ph['g'][1]['last']]))


# ===================================================================================================================== hint boxes
HINTS = [('hsav', '0_sav', 0x835, 'healthsav'), ('hoc', '0_oc', 0x836, 'healthoc'), ('hevo', '0_evo', 0x837, 'healthevo'), ('hrf', '0_rf', 0x834, 'healthrf')]


def test_hint():
    for panel, opener, pid, hl in HINTS:
        specs = [(0, pid, 0, 100, '-')]
        for idx in range(4):
            r = Run()
            r.step('open', 'openpanel health %s %d' % (opener, idx), 'pstate', 'phot', 'logs', *read_cmds(specs), shot(panel) if idx == 0 else 'echo -')
            r.go()
            L = r.lines('open')
            st, g = pstate(L), gets(L)
            bad = open_expect(panel, st, L, panel, opener, '', hl, 0, '', -1)
            if st:
                bad += gauge_problems(st, specs, g) + ['gauge text mismatch' for k, v in tchk(L) if v != 1]
                if st['g'][0]['text'] != (180, 74, 210, 84):
                    bad.append('text rect %r' % (st['g'][0]['text'],))
                if prect(L)[:2] != (394, 94):
                    bad.append('panel at %r' % (prect(L),))
            bad += log_problems(L)
            check(not bad, 'hint box %s opens at (394,94) from hotspot %d of %s on the health page: highlight %s/0, biome score %#x as bar and text (%.2f)' %
                  (panel, idx, opener, hl, pid, g[('-', pid)][1] if ('-', pid) in g else -1), '; '.join(bad))
        r = Run()
        r.step('open', 'openpanel health %s' % opener)
        r.step('c', 'ovr - %d 0 41.35' % pid, 'advance 400', 'pstate', 'advance 1200', 'pstate', *read_cmds(specs))
        r.go()
        pc = [pstate([l]) for l in r.lines('c') if l.startswith('pstate')]
        bad = gauge_problems(pc[1], specs, gets(r.lines('c')))
        check(not bad and pc[0]['g'][0]['last'] != 41.35 and pc[1]['g'][0]['last'] == 41.35 and all(v == 1 for k, v in tchk(r.lines('c'))),
              'hint box %s: the score follows the game after the poll (41.35)' % panel, '; '.join(bad))
    # the hint boxes link to each other: a click on the other boxes' hotspots opens them (verb 0x1a), the gauge follows the new page


def test_links():
    # the hint boxes link to each other (hotspots with verb 0x1a): every link opens its box at the same place with the game's value
    for panel, opener, pid, hl in HINTS:
        r = Run()
        r.step('open', 'openpanel health %s' % opener, 'phot')
        r.go()
        L = r.lines('open')
        links = [re.match(r'phot hotspot (\d+) (\d+) (\d+) (\d+) flags=\d+ verb=1a a=(\d+)', l) for l in L if l.startswith('phot hotspot')]
        pr = prect(L)
        seen = []
        for m in links:
            if not m:
                continue
            cx, cy = pr[0] + (int(m.group(1)) + int(m.group(3))) // 2, pr[1] + (int(m.group(2)) + int(m.group(4))) // 2
            r2 = Run()
            r2.step('x', 'openpanel health %s' % opener, 'click %d %d' % (cx, cy), 'pstate', 'logs')
            r2.go()
            st = pstate(r2.lines('x'))
            seen.append((st['panel'], st['g'][0]['param']) if st else None)
        want = [(h[0], h[2]) for h in HINTS if h[0] != panel]
        check(sorted(seen) == sorted(want), 'hint box %s: its three links open the other hint boxes (%s)' % (panel, ', '.join(w[0] for w in want)), repr(seen))


REOPEN = [('watpur', '7_algo1', 0), ('ocoxy', '6_bubo1', 0), ('oxyair', '3_oxy1', 0), ('pod', 'podrf', 0), ('pollut', '18_locks1', 0), ('pollut', '18_lockr1', 0), ('watpur', '13_condr1', 0),
          ('watpur', '4_subs3', 0), ('watpur', '4_subr2', 0), ('power', '1_res', 0), ('power', '3_wind', 0), ('power', '4_sol', 0), ('health', '0_sav', 0), ('health', '0_oc', 1),
          ('health', '0_evo', 2), ('health', '0_rf', 3)]


def test_reopen():
    for main, opener, idx in REOPEN:
        r = Run()
        r.step('a', 'openpanel %s %s %d' % (main, opener, idx), 'pstate', 'close', 'state', 'openpanel %s %s %d' % (main, opener, idx), 'pstate')
        r.go()
        L = r.lines('a')
        ps = [pstate([l]) for l in L if l.startswith('pstate')]
        closed = [l for l in L if l.startswith('state ')]
        check(len(ps) == 2 and ps[0]['raw'] == ps[1]['raw'] and 'panel=-' in closed[0], 'close and reopen of %s from %s: the panel goes away and comes back with exactly the same state' % (opener, main), '')


ALL = ['algs', 'bubl', 'oxy', 'pod', 'lock', 'cond', 'sub', 'power', 'hint', 'links', 'reopen']

if __name__ == '__main__':
    only = sys.argv[1:] or ALL
    if not os.path.exists(BIN):
        print('FAIL missing %s' % BIN)
        sys.exit(1)
    for name in only:
        globals()['test_' + name]()
    print('%d passed, %d failed' % (passes, fails))
    print('ALL OK' if not fails else '%d FAILED' % fails)
    sys.exit(1 if fails else 0)
